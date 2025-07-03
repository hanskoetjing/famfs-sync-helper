
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <net/sock.h>
#include <linux/inet.h>
#include <linux/types.h>

static struct socket *client_socket;
static struct sockaddr_in client_sockaddr;

/**
 * single_send_offset_data
 * single_send_offset_data - one shot of sending data to specific ip and port using TCP
 * @server_address:IPv4 address
 * @port: port number
 * @data: data to be send
 *
 * Return: 0 on success. Error otherwise.
 */
SYSCALL_DEFINE3(single_send_offset_data, char *, server_address, int, port, char *, data)
{
    int ret = 0;
    char ip_4_addr[16] = {0};
    int len = strscpy(ip_4_addr, server_address, sizeof(ip_4_addr));
	printk(KERN_INFO "Server address: %s port: %d\n", server_address, port);
	
    if (!client_socket) {
		ret = sock_create_kern(&init_net, AF_INET, SOCK_STREAM, IPPROTO_TCP, &client_socket);
		if (ret < 0) return ret;
		memset(&client_sockaddr, 0, sizeof(client_sockaddr));
		//initialise client socket address
		client_sockaddr.sin_family = AF_INET;
		client_sockaddr.sin_port = htons(port);
		int ret_ip = in4_pton(ip_4_addr, INET_ADDRSTRLEN, (u8 *)&client_sockaddr.sin_addr.s_addr, -1, NULL);
		if (ret_ip == 0) return -EINVAL;
		ret = client_socket->ops->connect(client_socket, (struct sockaddr *)&client_sockaddr, sizeof(client_sockaddr), 0);
		if (ret < 0) return ret;

        char msg[32] = {0};
        int len = strscpy(msg, data, sizeof(msg));
        pr_info("Sending message %s length %lu\n", msg, len);

        struct msghdr hdr;
		memset(&hdr, 0, sizeof(hdr));
		struct kvec iov = {
			.iov_base = msg,
			.iov_len = sizeof(msg)
		};
		kernel_sendmsg(client_socket, &hdr, &iov, 1, strlen(msg));
        sock_release(client_socket);
	    client_socket = NULL;
	} else {
		pr_info("There is a client socket\n");
		ret = -1;
	}
    
    return ret;
}


/**
 * start_tcp_client
 * start_tcp_client - starting a tcp client for continuous connection (for one shot, use the  single_send_offset_data)
 * @server_address:IPv4 address
 * @port: port number
 *
 * Return: 0 on success. Error otherwise.
 */
SYSCALL_DEFINE2(start_tcp_client, char *, server_address, int, port)
{
    int ret = 0;
    char ip_4_addr[16] = {0};
    int len = strscpy(ip_4_addr, server_address, sizeof(ip_4_addr));
	printk(KERN_INFO "Server address: %s port: %d\n", server_address, port);
	
    if (client_socket) {
		ret = sock_create_kern(&init_net, AF_INET, SOCK_STREAM, IPPROTO_TCP, &client_socket);
		if (ret < 0) return ret;
		memset(&client_sockaddr, 0, sizeof(client_sockaddr));
		//initialise client socket address
		client_sockaddr.sin_family = AF_INET;
		client_sockaddr.sin_port = htons(port);
		int ret_ip = in4_pton(ip_4_addr, INET_ADDRSTRLEN, (u8 *)&client_sockaddr.sin_addr.s_addr, -1, NULL);
		if (ret_ip == 0) return -EINVAL;
		ret = client_socket->ops->connect(client_socket, (struct sockaddr *)&client_sockaddr, sizeof(client_sockaddr), 0);
		if (ret < 0) return ret;
	} else {
		pr_info("There is a client socket\n");
		ret = -1;
	}
    
    return ret;
}

/**
 * send_data
 * send_data - send data to previously connected client socket
 * @data:data to be sent
 *
 * Return: 0 on success. Error otherwise.
 */
SYSCALL_DEFINE1(send_data, char *, data)
{
    int ret = 0;
    if (client_socket) {
		char msg[32] = {0};
        int len = strscpy(msg, data, sizeof(msg));
        pr_info("Sending message %s length %lu\n", msg, len);

        struct msghdr hdr;
		memset(&hdr, 0, sizeof(hdr));
		struct kvec iov = {
			.iov_base = msg,
			.iov_len = sizeof(msg)
		};
		ret = kernel_sendmsg(client_socket, &hdr, &iov, 1, strlen(msg));
	} else {
		pr_info("There is no client socket\n");
		ret = -1;
	}
    
    return ret;
}

/**
 * stop_tcp_client
 * stop_tcp_client - close opened socket. If a continuous connection established, please call this to close it. 
 * No need to call this if using the single shot version
 *
 * Return: 0 on success. Error otherwise.
 */
SYSCALL_DEFINE0(stop_tcp_client)
{
    int ret = 0;
    if (client_socket) {
		pr_info("Disconnect from server %pI4\n", &client_sockaddr.sin_addr.s_addr);
		ret = sock_release(client_socket);
		client_socket = NULL;
	} else {
		pr_info("There is no client socket\n");
		ret = -1;
	}
    
    return ret;
}