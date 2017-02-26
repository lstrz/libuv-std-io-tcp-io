#include <uv.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define IP "localhost"
#define PORT "12345"
#define BUFFER_LEN 4096

#define check_uv(status) do { \
        int code = (status); \
        if(code < 0){ \
            fprintf(stderr, "%s: %s\n", uv_err_name(code), uv_strerror(code)); \
            exit(code); \
        } \
    } while(0)

#define memory_error(fmt, ...) do { \
        fprintf(stderr, "%s: %s (%d): not enough memory: " fmt "\n", __FILE__, __FUNCTION__, __LINE__, ## __VA_ARGS__); \
    } while (0)

uv_loop_t loop;
uv_signal_t sigterm;
uv_signal_t sigint;
uv_getaddrinfo_t getaddrinfo_req;
struct sockaddr addr;
uv_connect_t connect_req;
uv_tcp_t tcp;
uv_buf_t read_buffer;
uv_buf_t write_buffer;
uv_pipe_t stdin_pipe;
uv_pipe_t stdout_pipe;

void on_close(uv_handle_t* handle){
    if(!loop.active_handles){
        uv_stop(&loop);
    }
}

void on_walk(uv_handle_t* handle, void* arg){
    uv_close(handle, on_close);
}

void on_alloc_tcp(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf){
    *buf = read_buffer;
}

void on_alloc_stdin(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf){
    *buf = write_buffer;
}

void on_stdout_write(uv_write_t* req, int status){
    check_uv(status);
    if(req && req->data){
        free(req->data);
	}
    if(req){
        free(req);
	}
}

void on_tcp_read(uv_stream_t* handle, ssize_t nread, const uv_buf_t* buf){
    if (nread > 0){
        uv_write_t *req;
        if(!(req = malloc(sizeof(uv_write_t)))){
            memory_error();
		}
        uv_buf_t buffer = uv_buf_init(malloc(nread), nread);
        memcpy(buffer.base, buf->base, nread);
        req->data = buffer.base;
        check_uv(uv_write(req, (uv_stream_t*)&stdout_pipe, &buffer, 1, on_stdout_write));
    } else if (nread < 0) {
        if (nread == UV_EOF){
            uv_stop(&loop);
        }
    }
}

void on_tcp_write(uv_write_t* req, int status){
    check_uv(status);
    if(req && req->data){
        free(req->data);
	}
    if(req){
        free(req);
	}
}

void on_stdin_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf){
    if (nread > 0){
        uv_write_t *req;
        if(!(req = malloc(sizeof(uv_write_t)))){
            memory_error();
		}
        uv_buf_t buffer = uv_buf_init(malloc(nread), nread);
        memcpy(buffer.base, buf->base, nread);
        req->data = buffer.base;
        check_uv(uv_write(req, connect_req.handle, &buffer, 1, on_tcp_write));
    } else if (nread < 0) {
        if (nread == UV_EOF){
            uv_stop(&loop);
        }
    }
}

void on_connect(uv_connect_t *connection, int status){
    check_uv(status);

    char node[INET_ADDRSTRLEN];
    check_uv(uv_ip4_name((const struct sockaddr_in *) &addr, node, INET_ADDRSTRLEN));
    printf("Connected to %s on port %hd!\n", node, ntohs(((struct sockaddr_in*)&addr)->sin_port));

    check_uv(uv_read_start(connection->handle, on_alloc_tcp, on_tcp_read));
    check_uv(uv_read_start((uv_stream_t*)&stdin_pipe, on_alloc_stdin, on_stdin_read));
}

void on_getaddrinfo(uv_getaddrinfo_t* req, int status, struct addrinfo* res){
    check_uv(status);
    addr = *res->ai_addr;
    uv_freeaddrinfo(res);

    check_uv(uv_tcp_connect(&connect_req, &tcp, &addr, on_connect));
}

static void on_signal(uv_signal_t *handle, int signum){
    uv_stop(&loop);
}

int main(void){
    check_uv(uv_loop_init(&loop));

    check_uv(uv_signal_init(&loop, &sigterm));
    check_uv(uv_signal_start(&sigterm, on_signal, SIGTERM));
    uv_unref((uv_handle_t *) &sigterm);
    check_uv(uv_signal_init(&loop, &sigint));
    check_uv(uv_signal_start(&sigint, on_signal, SIGINT));
    uv_unref((uv_handle_t *) &sigint);

    read_buffer = uv_buf_init(malloc(BUFFER_LEN), BUFFER_LEN);
    write_buffer = uv_buf_init(malloc(BUFFER_LEN), BUFFER_LEN);
    check_uv(uv_getaddrinfo(&loop, &getaddrinfo_req, on_getaddrinfo, IP, PORT, NULL));
    check_uv(uv_tcp_init(&loop, &tcp));

    check_uv(uv_pipe_init(&loop, &stdin_pipe, 0));
    check_uv(uv_pipe_open(&stdin_pipe, 0));
    check_uv(uv_pipe_init(&loop, &stdout_pipe, 0));
    check_uv(uv_pipe_open(&stdout_pipe, 1));

    check_uv(uv_run(&loop, UV_RUN_DEFAULT));

    check_uv(uv_signal_stop(&sigterm));
    check_uv(uv_signal_stop(&sigint));
    check_uv(uv_read_stop(connect_req.handle));
    check_uv(uv_read_stop((uv_stream_t*)&stdin_pipe));
    if(read_buffer.base)
        free(read_buffer.base);
    if(write_buffer.base)
        free(write_buffer.base);

    uv_walk(&loop, on_walk, NULL);
    check_uv(uv_run(&loop, UV_RUN_DEFAULT));
    check_uv(uv_loop_close(&loop));

    return 0;
}