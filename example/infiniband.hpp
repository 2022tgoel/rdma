#pragma once

#include <iostream>
#include <infiniband/verbs.h>

int msg(struct ibv_device *device, int tcp_socket)
{

    struct ibv_context *ctx;

    ctx = ibv_open_device(device);
    if (!ctx)
    {
        std::cerr << "Error, failed to open the device " << ibv_get_device_name(device) << std::endl;
        return 1;
    }

    std::cout << "The device " << ibv_get_device_name(ctx->device) << " was opened." << std::endl;

    struct ibv_pd *protection_domain = ibv_alloc_pd(ctx);
    int cq_size = 0x10;
    struct ibv_cq *completion_queue = ibv_create_cq(ctx, cq_size, nullptr, nullptr, 0);

    struct ibv_qp_init_attr queue_pair_init_attr;
    memset(&queue_pair_init_attr, 0, sizeof(queue_pair_init_attr));
    queue_pair_init_attr.qp_type = IBV_QPT_RC;
    // queue_pair_init_attr.sq_sig_all = 1;             // if not set 0, all work requests submitted to SQ will always generate a Work Completion.
    queue_pair_init_attr.send_cq = completion_queue; // completion queue can be shared or you can use distinct completion queues.
    queue_pair_init_attr.recv_cq = completion_queue; // completion queue can be shared or you can use distinct completion queues.
    queue_pair_init_attr.cap.max_send_wr = 1;        // increase if you want to keep more send work requests in the SQ.
    queue_pair_init_attr.cap.max_recv_wr = 1;        // increase if you want to keep more receive work requests in the RQ.
    queue_pair_init_attr.cap.max_send_sge = 1;       // increase if you allow send work requests to have multiple scatter gather entry (SGE).
    queue_pair_init_attr.cap.max_recv_sge = 1;       // increase if you allow receive work requests to have multiple scatter gather entry (SGE).

    struct ibv_qp *send_recv_queue = ibv_create_qp(protection_domain, &queue_pair_init_attr);

    int ib_port = 1;
    ibv_port_attr port_attr;
    ibv_query_port(ctx, ib_port, &port_attr);
    // Sends the information about your QP to the other node
    int message[2] = {send_recv_queue->qp_num, port_attr.lid};
    std::cout << "Queue Pair Num: " << send_recv_queue->qp_num << std::endl << "Port: " << port_attr.lid << std::endl;
    send(tcp_socket, message, 8, 0);

    struct ibv_qp_attr attr;
    // move the QP into init
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = ibv_qp_state::IBV_QPS_INIT;
    attr.port_num = ib_port;
    attr.pkey_index = 0;
    attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
    if (ibv_modify_qp(send_recv_queue, &attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS))
    {
        std::cerr << "Error updating the queue pair to INIT" << std::endl;
        return 1;
    }
    
    // Move the QP into RTR
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = ibv_qp_state::IBV_QPS_RTR;
    attr.path_mtu = ibv_mtu::IBV_MTU_1024;
    attr.rq_psn = 0;
    attr.max_dest_rd_atomic = 1;
    attr.min_rnr_timer = 0x12;
    attr.ah_attr.is_global = 0;
    attr.ah_attr.sl = 0;
    attr.ah_attr.src_path_bits = 0;
    attr.ah_attr.port_num = ib_port;

    // Receive the information about the other queue pair from the other node
    int buffer[2];
    if (read(tcp_socket, buffer, sizeof(buffer)) != sizeof(buffer))
    {
        std::cerr << "Failed to receive data" << std::endl;
        return 1;
    }
    
    std::cout << "Peer Queue Pair Num: " << buffer[0] << std::endl << "Port: " << buffer[1] << std::endl;

    attr.dest_qp_num = buffer[0];
    attr.ah_attr.dlid = buffer[1];

    if (ibv_modify_qp(send_recv_queue, &attr, IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER))
    {
        std::cerr << "Error updating the queue pair to RTR" << std::endl;
        return 1;
    }
    
    // Move the QP into RTS
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = ibv_qp_state::IBV_QPS_RTS;
    attr.timeout = 0x12;
    attr.retry_cnt = 7;
    attr.rnr_retry = 7;
    attr.sq_psn = 0;
    attr.max_rd_atomic = 1;
    if (ibv_modify_qp(send_recv_queue, &attr, IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC))
    {
        std::cerr << "Error updating the queue pair to RTS" << std::endl;
        return 1;
    }
    
    char hello[] = "Hello world";
    char hello_recv[12];
    
    struct ibv_mr* recv_mr = ibv_reg_mr(protection_domain, hello_recv, sizeof(hello_recv), IBV_ACCESS_LOCAL_WRITE);
    struct ibv_mr* send_mr = ibv_reg_mr(protection_domain, hello, sizeof(hello), 0);
    
    // Create a recv WR
    {
        struct ibv_sge sg;
        struct ibv_recv_wr wr;
        struct ibv_recv_wr *bad_wr;
        
        memset(&sg, 0, sizeof(sg));
        sg.addr	  = (uintptr_t)hello_recv;
        sg.length = sizeof(hello_recv);
        sg.lkey	  = recv_mr->lkey;
        
        memset(&wr, 0, sizeof(wr));
        wr.wr_id      = 100;
        wr.sg_list    = &sg;
        wr.num_sge    = 1;
        wr.next = nullptr;
        
        if (ibv_post_recv(send_recv_queue, &wr, &bad_wr)) {
        	std::cerr << "Error, ibv_post_recv() failed" << std::endl;
        	return 1;
        } else {
            std::cout << "Made receive request" << std::endl;
        }
    }
    
    // Create a send WR
    {
        struct ibv_sge sg;
        struct ibv_send_wr wr;
        struct ibv_send_wr* bad_wr;
        
        memset(&sg, 0, sizeof(sg));
        sg.addr	  = (uintptr_t)hello;
        sg.length = sizeof(hello);
        sg.lkey	  = send_mr->lkey;
        
        memset(&wr, 0, sizeof(wr));
        wr.wr_id      = 200;
        wr.sg_list    = &sg;
        wr.num_sge    = 1;
        wr.opcode = IBV_WR_SEND;
        wr.next = nullptr;
        
        if (ibv_post_send(send_recv_queue, &wr, &bad_wr)) {
        	std::cerr << "Error, ibv_post_send() failed" << std::endl;
        	return 1;
        } else {
            std::cout << "Made send request" << std::endl;
        }
    }
        
    // Poll for completion in the WC queue
    struct ibv_wc wc;
    int result;
    do {
        // ibv_poll_cq returns the number of WCs that are newly completed,
        // If it is 0, it means no new work completion is received.
        // Here, the second argument specifies how many WCs the poll should check,
        // however, giving more than 1 incurs stack smashing detection with g++8 compilation.
        result = ibv_poll_cq(completion_queue, 1, &wc);
    } while (result == 0);
    
    if (result > 0 && wc.status == ibv_wc_status::IBV_WC_SUCCESS) {
        std::cout << "Got message: " << hello_recv << std::endl;
    } else {
        std::cerr << "Poll failed with status " << ibv_wc_status_str(wc.status) << ", Work request: " << wc.wr_id << std::endl;
    }

    int rc = ibv_close_device(ctx);
    if (rc)
    {
        std::cerr << "Error, failed to close the device " << ibv_get_device_name(ctx->device) << std::endl;
        return rc;
    }

    return 0;
}

int infiniband(int socket)
{
    struct ibv_device **device_list;
    int num_devices;
    int i;

    device_list = ibv_get_device_list(&num_devices);
    if (!device_list)
    {
        std::cerr << "Error, ibv_get_device_list() failed" << std::endl;
        return -1;
    }

    if (num_devices != 1)
    {
        std::cerr << "Devices found is not expected value of 1." << std::endl;
        ibv_free_device_list(device_list);
        return -1;
    }

    msg(device_list[0], socket);
    ibv_free_device_list(device_list);
}