#pragma once

#include <iostream>
#include <infiniband/verbs.h>

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

int msg(struct ibv_device *device, int tcp_socket)
{

    struct ibv_context *ctx;

    ctx = ibv_open_device(device);
    if (!ctx)
    {
        std::cerr << "Error, failed to open the device " << ibv_get_device_name(device) << std::endl;
        return -1;
    }

    std::cout << "The device " << ibv_get_device_name(ctx->device) << " was opened." << std::endl;

    struct ibv_pd *protection_domain = ibv_alloc_pd(ctx);
    int cq_size = 0x10;
    struct ibv_cq *completion_queue = ibv_create_cq(ctx, cq_size, nullptr, nullptr, 0);

    struct ibv_qp_init_attr queue_pair_init_attr;
    memset(&queue_pair_init_attr, 0, sizeof(queue_pair_init_attr));
    queue_pair_init_attr.qp_type = IBV_QPT_RC;
    queue_pair_init_attr.sq_sig_all = 1;             // if not set 0, all work requests submitted to SQ will always generate a Work Completion.
    queue_pair_init_attr.send_cq = completion_queue; // completion queue can be shared or you can use distinct completion queues.
    queue_pair_init_attr.recv_cq = completion_queue; // completion queue can be shared or you can use distinct completion queues.
    queue_pair_init_attr.cap.max_send_wr = 1;        // increase if you want to keep more send work requests in the SQ.
    queue_pair_init_attr.cap.max_recv_wr = 1;        // increase if you want to keep more receive work requests in the RQ.
    queue_pair_init_attr.cap.max_send_sge = 1;       // increase if you allow send work requests to have multiple scatter gather entry (SGE).
    queue_pair_init_attr.cap.max_recv_sge = 1;       // increase if you allow receive work requests to have multiple scatter gather entry (SGE).

    struct ibv_qp *send_recv_queue = ibv_create_qp(protection_domain, &queue_pair_init_attr);

    int ib_port = 1;
    ibv_port_attr port_attr;
    // Sends the information about your QP to the other node
    int message[2] = {send_recv_queue->qp_num, ibv_query_port(ctx, ib_port, &port_attr)};
    send(tcp_socket, message, 8, 0);

    struct ibv_qp_attr rtr_attr;
    memset(&rtr_attr, 0, sizeof(rtr_attr));
    rtr_attr.qp_state = ibv_qp_state::IBV_QPS_RTR;
    rtr_attr.path_mtu = ibv_mtu::IBV_MTU_1024;
    rtr_attr.rq_psn = 0;
    rtr_attr.max_dest_rd_atomic = 1;
    rtr_attr.min_rnr_timer = 0x12;
    rtr_attr.ah_attr.is_global = 0;
    rtr_attr.ah_attr.sl = 0;
    rtr_attr.ah_attr.src_path_bits = 0;
    rtr_attr.ah_attr.port_num = ib_port;

    // Receive the information about the other queue pair from the other node
    int buffer[2];
    if (read(tcp_socket, buffer, sizeof(buffer)) != sizeof(buffer))
    {
        std::cerr << "Failed to receive data" << std::endl;
        return 1;
    }

    rtr_attr.dest_qp_num = buffer[1];
    rtr_attr.ah_attr.dlid = buffer[2];

    if (ibv_modify_qp(send_recv_queue, &rtr_attr, IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER))
    {
        std::cerr << "Error updating the queue pair to RTR" << std::endl;
    }

    int rc = ibv_close_device(ctx);
    if (rc)
    {
        std::cerr << "Error, failed to close the device " << ibv_get_device_name(ctx->device) << std::endl;
        return rc;
    }
}