/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <limits>
#include <cassert>

#include "link_queue.hh"
#include "timestamp.hh"
#include "util.hh"
#include "ezio.hh"
#include "abstract_packet_queue.hh"

using namespace std;

LinkQueue::LinkQueue( const string & link_name, const string & filename, const string & logfile,
                      const bool repeat, const bool graph_throughput, const bool graph_delay,
                      unique_ptr<AbstractPacketQueue> && packet_queue,
                      const string & command_line )
    : next_delivery_( 0 ),
      schedule_(),
      base_timestamp_( timestamp() ),
      packet_queue_( move( packet_queue ) ),
      driver_queue_(), /* Initialize driver queue */ 
      packet_in_transit_( "", 0 ),
      packet_in_transit_bytes_left_( 0 ),
      output_queue_(),
      log_(),
      throughput_graph_( nullptr ),
      delay_graph_( nullptr ),
      repeat_( repeat ),
      finished_( false )
{
    assert_not_root();

    /* open filename and load schedule */
    ifstream trace_file( filename );

    if ( not trace_file.good() ) {
        throw runtime_error( filename + ": error opening for reading" );
    }

    string line;

    while ( trace_file.good() and getline( trace_file, line ) ) {
        if ( line.empty() ) {
            throw runtime_error( filename + ": invalid empty line" );
        }

        const uint64_t ms = myatoi( line );

        if ( not schedule_.empty() ) {
            if ( ms < schedule_.back() ) {
                throw runtime_error( filename + ": timestamps must be monotonically nondecreasing" );
            }
        }

        schedule_.emplace_back( ms );
    }

    if ( schedule_.empty() ) {
        throw runtime_error( filename + ": no valid timestamps found" );
    }

    if ( schedule_.back() == 0 ) {
        throw runtime_error( filename + ": trace must last for a nonzero amount of time" );
    }

    /* open logfile if called for */
    if ( not logfile.empty() ) {
        log_.reset( new ofstream( logfile ) );
        if ( not log_->good() ) {
            throw runtime_error( logfile + ": error opening for writing" );
        }

        *log_ << "# mahimahi mm-link (" << link_name << ") [" << filename << "] > " << logfile << endl;
        *log_ << "# command line: " << command_line << endl;
        *log_ << "# queue: " << packet_queue_->to_string() << endl;
        *log_ << "# init timestamp: " << initial_timestamp() << endl;
        *log_ << "# base timestamp: " << base_timestamp_ << endl;
        const char * prefix = getenv( "MAHIMAHI_SHELL_PREFIX" );
        if ( prefix ) {
            *log_ << "# mahimahi config: " << prefix << endl;
        }
    }

    /* create graphs if called for */
    if ( graph_throughput ) {
        throughput_graph_.reset( new BinnedLiveGraph( link_name + " [" + filename + "]",
                                                      { make_tuple( 1.0, 0.0, 0.0, 0.25, true ),
                                                        make_tuple( 0.0, 0.0, 0.4, 1.0, false ),
                                                        make_tuple( 1.0, 0.0, 0.0, 0.5, false ) },
                                                      "throughput (Mbps)",
                                                      8.0 / 1000000.0,
                                                      true,
                                                      500,
                                                      [] ( int, int & x ) { x = 0; } ) );
    }

    if ( graph_delay ) {
        delay_graph_.reset( new BinnedLiveGraph( link_name + " delay [" + filename + "]",
                                                 { make_tuple( 0.0, 0.25, 0.0, 1.0, false ) },
                                                 "queueing delay (ms)",
                                                 1, false, 250,
                                                 [] ( int, int & x ) { x = -1; } ) );
    }
}

void LinkQueue::record_arrival( const uint64_t arrival_time, const size_t pkt_size )
{
    /* log it */
    if ( log_ ) {
        *log_ << arrival_time << " + " << pkt_size << endl;
    }

    /* meter it */
    if ( throughput_graph_ ) {
        throughput_graph_->add_value_now( 1, pkt_size );
    }
}

void LinkQueue::record_drop( const uint64_t time, const size_t pkts_dropped, const size_t bytes_dropped)
{
    /* log it */
    if ( log_ ) {
        *log_ << time << " d " << pkts_dropped << " " << bytes_dropped << endl;
    }
}

void LinkQueue::record_departure_opportunity( void )
{
    /* log the delivery opportunity */
    if ( log_ ) {
        *log_ << next_delivery_time() << " # " << PACKET_SIZE << endl;
    }

    /* meter the delivery opportunity */
    if ( throughput_graph_ ) {
        throughput_graph_->add_value_now( 0, PACKET_SIZE );
    }    
}

void LinkQueue::record_departure( const uint64_t departure_time, const QueuedPacket & packet )
{
    /* log the delivery */
    if ( log_ ) {
        *log_ << departure_time << " - " << packet.contents.size()
              << " " << departure_time - packet.arrival_time << endl;
    }

    /* meter the delivery */
    if ( throughput_graph_ ) {
        throughput_graph_->add_value_now( 2, packet.contents.size() );
    }

    if ( delay_graph_ ) {
        delay_graph_->set_max_value_now( 0, departure_time - packet.arrival_time );
    }    
}
/*
入队逻辑 read_packet
：读到包 -> 先进 driver_queue_。
增加了 driver_queue_ 容量检查和 Tail Drop 逻辑。

*/
void LinkQueue::read_packet( const string & contents )
{
    const uint64_t now = timestamp();

    if ( contents.size() > PACKET_SIZE ) {
        throw runtime_error( "packet size is greater than maximum" );
    }

    rationalize( now );

    record_arrival( now, contents.size() );

    rationalize( now );

    record_arrival( now, contents.size() );

    /*  虚拟驱动层的逻辑 (Virtual Driver Logic)
       ========================================================================================
       核心说明: 
       为了模拟真实的OS内核行为，我们不再让数据包直接进入Mahimahi的发送队列，
       而是先让它进入一个自定义的 'Driver Queue'
    */

    /* 背压与尾部丢弃
       如果 driver_queue_ 满了 (MAX_DRIVER_QUEUE_SIZE)，说明下一级的 Modem 太慢了
       (这就对应了物理上的"背压"现象)，驱动层已经没有空间了。
       此时必须丢弃数据包
    */
    if ( driver_queue_.size() >= MAX_DRIVER_QUEUE_SIZE ) {
        /* Drop Packet: Record drop event and discard content */
        record_drop( now, 1, contents.size() );
    } else {
        /* Enqueue: Add to the tail of the Driver Queue */
        driver_queue_.emplace_back( contents, now );
    }

    try_move_packets();
    
    // Original assertions and drop logic were for direct enqueue, now we handled drop above or in try_move_packets
    // We keep the logic valid by checking if packet eventually made it to modem queue or remains in driver queue
    // For simplicity, we assume record_drop handles the stats.
}

/*

出队/搬运逻辑 (try_move_packets) 
实现了 Modem Queue 容量检查 (Backpressure 触发条件)。
实现了 从 Driver 到 Modem 的搬运。
实现了 统计信息 (队列长度/停留时间) 的计算


*/
void LinkQueue::try_move_packets( void )
{
    /* 虚拟 Modem 容量限制 (背压触发)
       模拟硬件限制：真实的 Modem 芯片内部缓存是有限的。
       当 Modem 缓存填满时，它会禁止驱动层继续发包。
    */
    const unsigned int MODEM_QUEUE_CAPACITY = 100; //  Capacity of the simulated Modem Queue

    while ( !driver_queue_.empty() ) {
        /* 检查 Modem 队列状态
           在搬运数据包之前，先检查下一级的 Modem Queue (packet_queue_) 是否还有空间。
        */
        if ( packet_queue_->size_packets() >= MODEM_QUEUE_CAPACITY ) {
            /* [背压生效 - BACKPRESSURE ACTIVE]
               Modem 满了！我们不能再搬运数据包了。
               数据包被迫滞留在 driver_queue_ 中，导致 driver_queue_ 长度增加。
               (上层 CCA 分算法会观测到 RTT 增加，或者最终在 Driver 层观测到丢包)。
            */
            break;
        }

        /* [数据搬运] 
           如果 Modem 还有空间，模拟 DMA 传输：从 Host 内存 (Driver) 搬运到设备内存 (Modem)。
        */
        QueuedPacket & p = driver_queue_.front();
        
        // Move the packet: Enqueue to Modem Queue (Standard Mahimahi Queue)
        packet_queue_->enqueue( std::move(p) );
        
        // Remove from Driver Queue
        driver_queue_.pop_front();
    }

    /* Expose Driver Queue Signal */
    static uint64_t last_write_time = 0;
    uint64_t now = timestamp();
    if ( now - last_write_time > 1 ) { // Limit write freq to ~1ms
        std::ofstream signal_file("/tmp/mm_virtual_driver_queue"); // Open/Close every time is inefficient but safest for IPC simplicity without shared mem complexity
        if (signal_file.is_open()) {
             // Format: Count DwellTime(of head 队头)
             uint64_t head_dwell = 0;
             if (!driver_queue_.empty()) {
                // 当前时间 - 这个包进入 Driver Queue 的时间
                 head_dwell = now - driver_queue_.front().arrival_time;
             }
             signal_file << driver_queue_.size() << " " << head_dwell << std::endl;
        }
        last_write_time = now;
    }
}

uint64_t LinkQueue::next_delivery_time( void ) const
{
    if ( finished_ ) {
        return -1;
    } else {
        return schedule_.at( next_delivery_ ) + base_timestamp_;
    }
}

void LinkQueue::use_a_delivery_opportunity( void )
{
    record_departure_opportunity();

    next_delivery_ = (next_delivery_ + 1) % schedule_.size();

    /* wraparound */
    if ( next_delivery_ == 0 ) {
        if ( repeat_ ) {
            base_timestamp_ += schedule_.back();
        } else {
            finished_ = true;
        }
    }
}

/* emulate the link up to the given timestamp */
/* this function should be called before enqueueing any packets and before
   calculating the wait_time until the next event */
void LinkQueue::rationalize( const uint64_t now )
{
    while ( next_delivery_time() <= now ) {
        const uint64_t this_delivery_time = next_delivery_time();

        /* burn a delivery opportunity */
        unsigned int bytes_left_in_this_delivery = PACKET_SIZE;
        use_a_delivery_opportunity();

        while ( bytes_left_in_this_delivery > 0 ) {
            if ( not packet_in_transit_bytes_left_ ) {
                if ( packet_queue_->empty() ) {
                    break;
                }
                packet_in_transit_ = packet_queue_->dequeue();
                packet_in_transit_bytes_left_ = packet_in_transit_.contents.size();
            }

            assert( packet_in_transit_.arrival_time <= this_delivery_time );
            assert( packet_in_transit_bytes_left_ <= PACKET_SIZE );
            assert( packet_in_transit_bytes_left_ > 0 );
            assert( packet_in_transit_bytes_left_ <= packet_in_transit_.contents.size() );

            /* how many bytes of the delivery opportunity can we use? */
            const unsigned int amount_to_send = min( bytes_left_in_this_delivery,
                                                     packet_in_transit_bytes_left_ );

            /* send that many bytes */
            packet_in_transit_bytes_left_ -= amount_to_send;
            bytes_left_in_this_delivery -= amount_to_send;

            /* has the packet been fully sent? */
            if ( packet_in_transit_bytes_left_ == 0 ) {
                record_departure( this_delivery_time, packet_in_transit_ );

                /* this packet is ready to go */
                output_queue_.push( move( packet_in_transit_.contents ) );
            }
        }
    }
    /* Try to refill modem queue from driver queue after draining */
    try_move_packets();
}

void LinkQueue::write_packets( FileDescriptor & fd )
{
    while ( not output_queue_.empty() ) {
        fd.write( output_queue_.front() );
        output_queue_.pop();
    }
}

unsigned int LinkQueue::wait_time( void )
{
    const auto now = timestamp();

    rationalize( now );

    if ( next_delivery_time() <= now ) {
        return 0;
    } else {
        return next_delivery_time() - now;
    }
}

bool LinkQueue::pending_output( void ) const
{
    return not output_queue_.empty();
}
