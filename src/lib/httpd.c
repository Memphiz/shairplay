#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "httpd.h"
#include "netutils.h"
#include "http_request.h"
#include "compat.h"
#include "logger.h"

struct http_connection_s {
	int connected;

	int socket_fd;
	void *user_data;
	http_request_t *request;
};
typedef struct http_connection_s http_connection_t;

struct httpd_s {
	logger_t *logger;
	httpd_callbacks_t callbacks;

	int use_rtsp;

	int max_connections;
	http_connection_t *connections;

	/* These variables only edited mutex locked */
	int running;
	int joined;
	thread_handle_t thread;
	mutex_handle_t run_mutex;

	/* Server fd for accepting connections */
	int server_fd;
};

httpd_t *
httpd_init(logger_t *logger, httpd_callbacks_t *callbacks, int max_connections, int use_rtsp)
{
	httpd_t *httpd;

	assert(logger);
	assert(callbacks);
	assert(max_connections > 0);

	/* Allocate the httpd_t structure */
	httpd = calloc(1, sizeof(httpd_t));
	if (!httpd) {
		return NULL;
	}

	httpd->use_rtsp = !!use_rtsp;
	httpd->max_connections = max_connections;
	httpd->connections = calloc(max_connections, sizeof(http_connection_t));
	if (!httpd->connections) {
		free(httpd);
		return NULL;
	}

	/* Use the logger provided */
	httpd->logger = logger;

	/* Save callback pointers */
	memcpy(&httpd->callbacks, callbacks, sizeof(httpd_callbacks_t));

	/* Initial status joined */
	httpd->running = 0;
	httpd->joined = 1;

	return httpd;
}

void
httpd_destroy(httpd_t *httpd)
{
	if (httpd) {
		httpd_stop(httpd);

		free(httpd->connections);
		free(httpd);
	}
}

static void
httpd_add_connection(httpd_t *httpd, int fd, unsigned char *local, int local_len, unsigned char *remote, int remote_len)
{
	int i;

	for (i=0; i<httpd->max_connections; i++) {
		if (!httpd->connections[i].connected) {
			break;
		}
	}
	if (i == httpd->max_connections) {
		logger_log(httpd->logger, LOGGER_INFO, "Max connections reached\n");
		shutdown(fd, SHUT_RDWR);
		closesocket(fd);
		return;
	}

	httpd->connections[i].socket_fd = fd;
	httpd->connections[i].connected = 1;
	httpd->connections[i].user_data = httpd->callbacks.conn_init(httpd->callbacks.opaque, local, local_len, remote, remote_len);
}

static void
httpd_remove_connection(httpd_t *httpd, http_connection_t *connection)
{
	if (connection->request) {
		http_request_destroy(connection->request);
		connection->request = NULL;
	}
	httpd->callbacks.conn_destroy(connection->user_data);
	shutdown(connection->socket_fd, SHUT_WR);
	closesocket(connection->socket_fd);
	connection->connected = 0;
}

static THREAD_RETVAL
httpd_thread(void *arg)
{
	httpd_t *httpd = arg;
	char buffer[1024];
	int i;

	assert(httpd);

	while (1) {
		fd_set rfds;
		struct timeval tv;
		int nfds=0;
		int ret;

		MUTEX_LOCK(httpd->run_mutex);
		if (!httpd->running) {
			MUTEX_UNLOCK(httpd->run_mutex);
			break;
		}
		MUTEX_UNLOCK(httpd->run_mutex);

		/* Set timeout value to 5ms */
		tv.tv_sec = 1;
		tv.tv_usec = 5000;

		/* Get the correct nfds value and set rfds */
		FD_ZERO(&rfds);
		FD_SET(httpd->server_fd, &rfds);
		nfds = httpd->server_fd+1;
		for (i=0; i<httpd->max_connections; i++) {
			int socket_fd;
			if (!httpd->connections[i].connected) {
				continue;
			}
			socket_fd = httpd->connections[i].socket_fd;
			FD_SET(socket_fd, &rfds);
			if (nfds <= socket_fd) {
				nfds = socket_fd+1;
			}
		}

		ret = select(nfds, &rfds, NULL, NULL, &tv);
		if (ret == 0) {
			/* Timeout happened */
			continue;
		} else if (ret == -1) {
			/* FIXME: Error happened */
			logger_log(httpd->logger, LOGGER_INFO, "Error in select\n");
			break;
		}

		if (FD_ISSET(httpd->server_fd, &rfds)) {
			struct sockaddr_storage remote_saddr;
			socklen_t remote_saddrlen;
			struct sockaddr_storage local_saddr;
			socklen_t local_saddrlen;
			unsigned char *local, *remote;
			int local_len, remote_len;
			int fd;

			remote_saddrlen = sizeof(remote_saddr);
			fd = accept(httpd->server_fd, (struct sockaddr *)&remote_saddr, &remote_saddrlen);
			if (fd == -1) {
				/* FIXME: Error happened */
				break;
			}

			local_saddrlen = sizeof(local_saddr);
			ret = getsockname(fd, (struct sockaddr *)&local_saddr, &local_saddrlen);
			if (ret == -1) {
				closesocket(fd);
				continue;
			}

			logger_log(httpd->logger, LOGGER_INFO, "Accepted client on socket %d\n", fd);
			local = netutils_get_address(&local_saddr, &local_len);
			remote = netutils_get_address(&remote_saddr, &remote_len);

			httpd_add_connection(httpd, fd, local, local_len, remote, remote_len);
		}
		for (i=0; i<httpd->max_connections; i++) {
			http_connection_t *connection = &httpd->connections[i];

			if (!connection->connected) {
				continue;
			}
			if (!FD_ISSET(connection->socket_fd, &rfds)) {
				continue;
			}

			/* If not in the middle of request, allocate one */
			if (!connection->request) {
				connection->request = http_request_init(httpd->use_rtsp);
				assert(connection->request);
			}

			logger_log(httpd->logger, LOGGER_DEBUG, "Receiving on socket %d\n", httpd->connections[i].socket_fd);
			ret = recv(connection->socket_fd, buffer, sizeof(buffer), 0);
			if (ret == 0) {
				logger_log(httpd->logger, LOGGER_INFO, "Connection closed\n");
				httpd_remove_connection(httpd, connection);
				continue;
			}

			/* Parse HTTP request from data read from connection */
			http_request_add_data(connection->request, buffer, ret);
			if (http_request_has_error(connection->request)) {
				logger_log(httpd->logger, LOGGER_INFO, "Error in parsing: %s\n", http_request_get_error_name(connection->request));
				httpd_remove_connection(httpd, connection);
				continue;
			}

			/* If request is finished, process and deallocate */
			if (http_request_is_complete(connection->request)) {
				http_response_t *response = NULL;

				httpd->callbacks.conn_request(connection->user_data, connection->request, &response);
				http_request_destroy(connection->request);
				connection->request = NULL;

				if (response) {
					const char *data;
					int datalen;
					int written;
					int ret;

					/* Get response data and datalen */
					data = http_response_get_data(response, &datalen);

					written = 0;
					while (written < datalen) {
						ret = send(connection->socket_fd, data+written, datalen-written, 0);
						if (ret == -1) {
							/* FIXME: Error happened */
							logger_log(httpd->logger, LOGGER_INFO, "Error in sending data\n");
							break;
						}
						written += ret;
					}
				} else {
					logger_log(httpd->logger, LOGGER_INFO, "Didn't get response\n");
				}
				http_response_destroy(response);
			}
		}
	}

	/* Remove all connections that are still connected */
	for (i=0; i<httpd->max_connections; i++) {
		http_connection_t *connection = &httpd->connections[i];

		if (!connection->connected) {
			continue;
		}
		logger_log(httpd->logger, LOGGER_INFO, "Removing connection\n");
		httpd_remove_connection(httpd, connection);
	}

	logger_log(httpd->logger, LOGGER_INFO, "Exiting thread\n");

	return 0;
}

int
httpd_start(httpd_t *httpd, unsigned short *port)
{
	assert(httpd);
	assert(port);

	MUTEX_LOCK(httpd->run_mutex);
	if (httpd->running || !httpd->joined) {
		MUTEX_UNLOCK(httpd->run_mutex);
		return 0;
	}

	httpd->server_fd = netutils_init_socket(port, 1, 0);
	if (httpd->server_fd == -1) {
		logger_log(httpd->logger, LOGGER_INFO, "Error initialising socket %d\n", SOCKET_GET_ERROR());
		MUTEX_UNLOCK(httpd->run_mutex);
		return -1;
	}
	if (listen(httpd->server_fd, 5) == -1) {
		logger_log(httpd->logger, LOGGER_INFO, "Error listening to socket\n");
		MUTEX_UNLOCK(httpd->run_mutex);
		return -2;
	}
	logger_log(httpd->logger, LOGGER_INFO, "Initialized server socket\n");

	/* Set values correctly and create new thread */
	httpd->running = 1;
	httpd->joined = 0;
	THREAD_CREATE(httpd->thread, httpd_thread, httpd);
	MUTEX_UNLOCK(httpd->run_mutex);

	return 1;
}

void
httpd_stop(httpd_t *httpd)
{
	assert(httpd);

	MUTEX_LOCK(httpd->run_mutex);
	if (!httpd->running || httpd->joined) {
		MUTEX_UNLOCK(httpd->run_mutex);
		return;
	}
	httpd->running = 0;
	MUTEX_UNLOCK(httpd->run_mutex);

	THREAD_JOIN(httpd->thread);

	MUTEX_LOCK(httpd->run_mutex);
	httpd->joined = 1;
	MUTEX_UNLOCK(httpd->run_mutex);
}

