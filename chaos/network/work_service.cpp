/*
 * Copyright 2012, Yunjie Lu.  All rights reserved.
 * https://github.com/lyjdamzwf/chaos
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the License file.
 */

#include "work_service.h"

/*! 
 *  @file           work_service.cpp
 *  @author         yunjie.lu
 *  @email          lyjdamzwf@gmail.com
 *  @weibo          http://weibo.com/crazyprogramerlyj
 *  @date           2012.4.17
 *  @brief          work service
 *  @last changed
 *
 */

namespace chaos
{

namespace network
{


work_service_t::work_service_t(const string& service_name_)
    :
        task_service_t(service_name_),
        m_enable_conn_heart_beat(false)
{
    m_conn_vct.resize(DEFAULT_CONN_VCT_SIZE, NULL);
}

work_service_t::~work_service_t()
{
}

void work_service_t::conn_timedout_callback(conn_id_t& conn_id_)
{
    connection_t::async_close(conn_id_, false, EV_TIMEOUT_CLOSED);

}

int work_service_t::start_heart_beat_service(const conn_heart_beat_param_t& param_)
{
    m_conn_heart_beat.set_callback_function(work_service_t::conn_timedout_callback);
    m_conn_heart_beat.set_timeout(param_.timeout_flag, param_.timeout);
    m_conn_heart_beat.set_max_limit(param_.max_limit_flag, param_.max_limit);
    m_enable_conn_heart_beat = true;
    m_conn_heart_beat.initialize(this);

    return m_conn_heart_beat.start();
}

int work_service_t::start(int32_t thread_num_)
{
    task_service_t::start(thread_num_);

    return 0;
}

int work_service_t::stop()
{
    this->post(async_method_t::bind_memfunc(this, &work_service_t::sync_close_all_conn_i));

    if (m_enable_conn_heart_beat)
    {
        m_conn_heart_beat.stop();
        m_enable_conn_heart_beat = false;
    }

    //! yunjie: 发送线程停止信号并pthread_join
    task_service_t::stop();

    return 0;
}


int work_service_t::async_add_connection(conn_ptr_t conn_ptr_)
{
    this->post(async_method_t::bind_memfunc(this, &work_service_t::sync_add_connection_i, conn_ptr_));

    return 0;
}


int work_service_t::async_del_connection(const conn_id_t& conn_id_)
{
    //! yunjie: 因为这里有可能被connection_t对象内部调用, 所以不需要当前上下文直接执行, 强制post到任务队列中等待下一轮执行
    this->post(async_method_t::bind_memfunc(this, &work_service_t::sync_del_connection_i, conn_id_));

    return 0;
}


void work_service_t::async_add_hb_element(conn_id_t& conn_id_)
{
    m_conn_heart_beat.async_add_element(conn_id_);
}

void work_service_t::async_update_hb_element(conn_id_t& conn_id_)
{
    m_conn_heart_beat.async_update_element(conn_id_);
}

void work_service_t::async_del_hb_element(conn_id_t& conn_id_)
{
    m_conn_heart_beat.async_del_element(conn_id_);
}

conn_ptr_t work_service_t::get_conn(const conn_id_t& conn_id_)
{
    fd_t peer_socket = conn_id_.socket;
    if (peer_socket >= m_conn_vct.size())
    {
        LOGWARN((WORK_SERVICE_MODULE, "work_service_t::get_conn fd is too big, return."));
        return NULL;
    }

    conn_ptr_t& conn_ptr = m_conn_vct[peer_socket];

    if (NULL != conn_ptr)
    {
        //! yunjie: 防止fd回绕, 根据时间戳判断是否的确是同一个connection, 有可能这个connection是新生成的"替身"连接
        if (
                conn_id_.timestamp.tv_sec != conn_ptr->get_timestamp().tv_sec
                ||
                conn_id_.timestamp.tv_usec != conn_ptr->get_timestamp().tv_usec
           )
        {
            LOGWARN((WORK_SERVICE_MODULE, "work_service_t::get_conn connection timestamp error, return. arg-[fd:%d]", peer_socket));
            return NULL;
        }
    }

    return conn_ptr;
}


int work_service_t::sync_close_all_conn_i()
{
    for (vector<conn_ptr_t>::iterator it = m_conn_vct.begin(); it != m_conn_vct.end(); ++it)
    {
        conn_ptr_t conn_ptr = *it;
        if (NULL != conn_ptr)
        {
            //! yunjie: 参数false - 由于是进程关闭时才会sync_close_all_conn_i, 所以不需要在关闭connection时从heart beat中删除. 否则会有大量"heart beat element not found"的警告日志
            connection_t::async_close(conn_ptr->get_conn_id(), false);
        }
    }

    return 0;
}

int work_service_t::sync_add_connection_i(conn_ptr_t conn_ptr_)
{
    if (is_recv_stop_signal())
    {
        //! yunjie: 如果收到停止信号, 那么不再接受新连接
        LOGWARN((WORK_SERVICE_MODULE, "work_service_t::sync_add_connection_i has recv stop signal, return"));
        return -1;
    }

    task_service_t* service_ptr = conn_ptr_->get_service_ptr();
    fd_t peer_socket = conn_ptr_->native_socket();

    if (peer_socket >= m_conn_vct.size())
    {
        m_conn_vct.resize(peer_socket);
        if (m_conn_vct.size() != peer_socket)
        {
            LOGWARN((WORK_SERVICE_MODULE, "work_service_t::sync_add_connection_i resize m_conn_vct failed arg-[fd:%d] return.", peer_socket));
            return -1;
        }
    }

    conn_ptr_t& conn_ptr = m_conn_vct[peer_socket];
    if (NULL != conn_ptr)
    {
        LOGWARN((WORK_SERVICE_MODULE, "work_service_t::sync_add_connection_i fd conflict arg-[fd:%d] return.", peer_socket));

        SAFE_DELETE(conn_ptr);      //! yunjie: 不需要在这里close socket, 析构时保证
    }
    conn_ptr = conn_ptr_;

    //! yunjie: 注册读事件, 为persist
    service_ptr->register_io_event(
                                    peer_socket,
                                    READ_EVENT_FLAG,
                                    &connection_t::on_peer_event,
                                    (void*)conn_ptr_,
                                    true
                                );
    
    //! yunjie: 注册错误处理事件, 无视persist
    service_ptr->register_io_event(
                                    peer_socket,
                                    ERROR_EVENT_FLAG,
                                    &connection_t::on_peer_event,
                                    (void*)conn_ptr_
                                );

    return 0;
}

int work_service_t::sync_del_connection_i(const conn_id_t& conn_id_)
{
    fd_t peer_socket = conn_id_.socket;
    if (peer_socket >= m_conn_vct.size())
    {
        LOGWARN((WORK_SERVICE_MODULE, "work_service_t::sync_del_connection_i fd is too big, return. arg-[fd:%d]", peer_socket));
        return -1;
    }

    conn_ptr_t& conn_ptr = m_conn_vct[peer_socket];

    if (NULL == conn_ptr)
    {
        LOGWARN((WORK_SERVICE_MODULE, "work_service_t::sync_del_connection_i the connection not found, return. arg-[fd:%d]", peer_socket));
        return -1;
    }

    //! yunjie: 防止fd回绕, 根据时间戳判断是否的确是同一个connection, 有可能这个connection是新生成的"替身"连接
    if (
        conn_id_.timestamp.tv_sec != conn_ptr->get_timestamp().tv_sec
        ||
        conn_id_.timestamp.tv_usec != conn_ptr->get_timestamp().tv_usec
       )
    {
        LOGWARN((WORK_SERVICE_MODULE, "work_service_t::sync_del_connection_i connection timestamp error, return. arg-[fd:%d]", peer_socket));
        return -1;
    }

    //! yunjie: 将conn_ptr置为NULL
    SAFE_DELETE(conn_ptr);
    
    return 0;
}


}

}
