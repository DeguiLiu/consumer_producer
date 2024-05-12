## ConsumerProducer库概述
ConsumerProducer库是一个用于多线程任务处理的C++库。它提供了一种机制，允许用户定义任务的优先级和处理方式，并通过多线程方式高效地处理任务队列中的任务。
+ 模块提供设置线程优先级、处理线程个数及任务队列个数的功能;
+ 模块提供低优先级队列和高优先级队列管理功能。
+ 生产者添加任务的时候会根据优先级添加到低优先级队列还是高优先级队列，消费者获取任务的时候优先获取高优先级队列中的任务进行处理。
+ 模块具有统计任务总的等待时间消耗，处理时间消耗、丢弃时间消耗信息的功能。

## ConsumerProducerConfig结构体
首先，让我们来看一下ConsumerProducerConfig结构体。这个结构体定义了任务处理的配置参数，如输入队列的名称、优先级、工作线程数量等。通过配置这些参数，用户可以根据实际需求灵活地调整任务处理的行为。

## ConsumerProducer类
ConsumerProducer类是ConsumerProducer库的核心组件，它提供了任务的添加、处理和管理功能。该类包含了任务队列、线程管理等重要功能，下面我们将详细介绍其主要成员和功能。

## MyJob结构体
MyJob结构体表示一个任务，包含了任务的相关信息，如任务指针、时间戳、任务ID等。通过MyJob结构体，用户可以轻松地管理和操作任务。

## MyCpQueue类
MyCpQueue类实现了一个任务队列，用于存储任务并支持任务的添加和弹出操作。该类采用循环队列的方式实现，保证了任务的高效处理。

## ConsumerProducer类成员函数
ConsumerProducer类提供了一系列成员函数，用于任务的添加、处理和管理。这些函数包括添加任务、启动处理线程、暂停任务处理等，为用户提供了丰富的操作接口。

| 函数名称   | 功能                              |
| ------------------------------------------ | ----------------------------------- |
| **ConsumerProducer:: ConsumerProducer**  | 构造函数                          |
|  ConsumerProducer::add_job              |  添加job到队列                   |
|  ConsumerProducer:: add_job_wait_done   | 添加**job**并等待任务完成         |
| **ConsumerProducer:: shutdown**          |  关闭线程                        |
| **ConsumerProducer:: queue_length**      | 获取队列中存在的**job**数量       |
| **ConsumerProducer:: max_queue_length**  | 获取队列可以存放的最大**job**数量 |
| **ConsumerProducer:: get_threads**       | 获取线程池中存放线程容器的首地址  |
| **ConsumerProducer:: dropped_job_count** | 获取丢弃的**job**数量             |
| **ConsumerProducer:: blocked_job_count** | 获取被阻塞的**job**数量           |
| **ConsumerProducer:: print_stats**       | 打印线程池状态信息                |
