#include "tgcommon.h"

pthread_mutex_t tg_content_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t tg_callbacks_mutex = PTHREAD_MUTEX_INITIALIZER;
tg_message_t * tasks_queue;
int tasks_queue_i;
tg_callback_t * tg_callbacks;
int tg_callbacks_i = 0;
char tg_token[64];
char tg_proxy[64];

int tg_set_proxy(const char * proxy)
{
	strcpy(tg_proxy, proxy);
	return SUCCESS;
}

int tg_start(json_object ** content_json, char * token)
{
	/* Global init must happen once, before any thread uses curl.
	 * Per-request curl_global_init()/cleanup() from several threads is
	 * not thread-safe and cleanup() races with in-flight transfers. */
	curl_global_init(CURL_GLOBAL_DEFAULT);
	snprintf(tg_token, sizeof(tg_token), "%s", token);
	tg_callbacks_init();
	pthread_t tg_get_content_thread;
	pthread_create(&tg_get_content_thread, NULL, tg_circle_handler, content_json);
	pthread_detach(tg_get_content_thread);
	return SUCCESS;
}

int tg_send_message(tg_message_t * msg)
{
	if (msg->chat_id <= 0) return -1;
	CURL * curl;
	tg_write_buf_t content_str;
	content_str.used = 0;

	curl = curl_easy_init();
	if (!curl) return -1;

	char * escaped = curl_easy_escape(curl, msg->text, 0);
	if (!escaped)
	{
		curl_easy_cleanup(curl);
		return -1;
	}

	/* Size the buffers to the actual URL, a long message must not overflow
	 * a fixed-size buffer. */
	size_t link_len = strlen(TG_BASIC_LINK) + strlen(tg_token) + 2;
	size_t sendmsg_len = link_len + strlen(TG_METHOD_SND)
	                     + strlen(TG_METHOD_SND_CHAT_ID) + strlen(TG_METHOD_SND_TEXT)
	                     + strlen(escaped) + 16;

	char * TG_link = malloc(link_len);
	char * TG_link_sendmsg = malloc(sendmsg_len);
	if (!TG_link || !TG_link_sendmsg)
	{
		curl_free(escaped);
		free(TG_link);
		free(TG_link_sendmsg);
		curl_easy_cleanup(curl);
		return -1;
	}

	snprintf(TG_link, link_len, "%s%s/", TG_BASIC_LINK, tg_token);
	snprintf(TG_link_sendmsg, sendmsg_len, "%s%s%s%s%d%s%s%s", TG_link, TG_METHOD_SND,
	         "?", TG_METHOD_SND_CHAT_ID, msg->chat_id,
	         "&", TG_METHOD_SND_TEXT, escaped);
	curl_free(escaped);

	curl_easy_setopt(curl, CURLOPT_URL, TG_link_sendmsg);
	if (*tg_proxy)
		curl_easy_setopt(curl, CURLOPT_PROXY, tg_proxy);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, tg_curl_write);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &content_str);

	CURLcode res = curl_easy_perform(curl);
	if(res != CURLE_OK)
	{
		fprintf(stderr, "curl_easy_perform() failed: %s\n",
		curl_easy_strerror(res));
		curl_easy_cleanup(curl);
		free(TG_link);
		free(TG_link_sendmsg);
		return 1;
	}

	curl_easy_cleanup(curl);

	free(TG_link);
	free(TG_link_sendmsg);
	return 0;
}

void * tg_circle_handler(void * arg)
{
	json_object ** content_json = (json_object **)arg;
	tg_queue_init();
	while (1)
	{
		tg_get_content(content_json);

		json_object * result_json = NULL;
		if (!json_object_object_get_ex(*content_json, "result", &result_json) || !result_json)
		{
			usleep(TG_INTERVAL);
			continue;
		}
		int messages_len = json_object_array_length(result_json);
		if (messages_len < 0) messages_len = 0;
		// TODO: Make arr sorted
		int update_id = 0;
		for (int i = 0; i < messages_len; i++)
		{
			json_object * message_json;
			message_json = json_object_array_get_idx(result_json, i);
			json_object * update_id_json;
			json_object_object_get_ex(message_json, "update_id", &update_id_json);
			update_id = json_object_get_int(update_id_json);
			json_object * message_content_json;
			if (!json_object_object_get_ex(message_json,
			                               "message",
			                               &message_content_json))
			{
				continue;
			}
			json_object * message_id_json;
			json_object_object_get_ex(message_content_json,
			                          "message_id",
			                          &message_id_json);

			tg_message_t * cur_msg = tg_message_init();

			cur_msg->message_id = json_object_get_int(message_id_json);

			json_object * chat_json;
			json_object * chat_id_json;
			if (!json_object_object_get_ex(message_content_json, "chat", &chat_json) ||
			    !json_object_object_get_ex(chat_json, "id", &chat_id_json))
			{
				free(cur_msg);
				continue;
			}
			cur_msg->chat_id = json_object_get_int(chat_id_json);

			json_object * text_json;
			json_object_object_get_ex(message_content_json, "text", &text_json);
			if (text_json)
				strncpy(cur_msg->text, json_object_get_string(text_json),
				        TG_MAX_MSG_LENGTH - 1); // leave the NUL from memset

			cur_msg->type = cur_msg->text[0] == '/' ? TG_MSG_COMMAND : TG_MSG_REGULAR;

			if (tg_try_callback(cur_msg))
			{
				tg_queue_put(cur_msg); // stores a copy
				free(cur_msg);
			}
		}
		tg_drop_messages(update_id);

		usleep(TG_INTERVAL);
	}
}

int tg_try_callback(tg_message_t * msg)
{
	if (msg->type == TG_MSG_COMMAND)
	{
		tg_callback_t clbk;

		char command[TG_MAX_MSG_LENGTH];
		tg_get_command(msg->text, command);

		if (!tg_callback_get(command, &clbk))
		{
			tg_clbk_thread_arg_t * targ =
				(tg_clbk_thread_arg_t *)malloc(sizeof(tg_clbk_thread_arg_t));
			if (!targ) return ERR_TRY_CLBK;
			targ->func = clbk.func;
			targ->msg = msg;
			pthread_t tg_clbk_thread;
			pthread_create(&tg_clbk_thread, NULL, tg_clbk_thread_wrapper, targ);
			pthread_detach(tg_clbk_thread);
			return SUCCESS;
		}
		return ERR_TRY_CLBK;
	}
	return ERR_TRY_CLBK_NOT_CMD;
}

void * tg_clbk_thread_wrapper(void * arg)
{
	tg_clbk_thread_arg_t * targ = (tg_clbk_thread_arg_t *)arg;
	targ->func(targ->msg); // no callback keeps the pointer
	free(targ->msg);
	free(targ);
	return NULL;
}

int tg_callback_bind(char * command, int (*callback_func)())
{
	tg_callback_t * tmp = (tg_callback_t *)realloc(tg_callbacks,
	                                    (tg_callbacks_i + 1) * sizeof(tg_callback_t));
	if (!tmp) return ERR_CLBK_BIND;
	tg_callbacks = tmp;

	tg_callback_t callback;
	snprintf(callback.command, sizeof(callback.command), "%s", command);
	callback.func = callback_func;

	pthread_mutex_lock(&tg_callbacks_mutex);
	tg_callbacks[tg_callbacks_i++] = callback;
	pthread_mutex_unlock(&tg_callbacks_mutex);
	return SUCCESS;
}

int tg_callback_remove(char * command)
{
	if (tg_callbacks_i)
	{
		tg_callback_t * clbks_tmp = (tg_callback_t *)malloc(
			(tg_callbacks_i - 1) * sizeof(tg_callback_t));
		if (!clbks_tmp) return ERR_CLBK_REMOVE;
		int k = 0, i = 0;
		pthread_mutex_lock(&tg_callbacks_mutex);
		for (; i < tg_callbacks_i; i++)
			if (strcmp(tg_callbacks[i].command, command))
				clbks_tmp[k++] = tg_callbacks[i];
		if (k < tg_callbacks_i) // at least one entry was removed
		{
			free(tg_callbacks);
			tg_callbacks = clbks_tmp;
			tg_callbacks_i = k;
			pthread_mutex_unlock(&tg_callbacks_mutex);
			return SUCCESS;
		}
		pthread_mutex_unlock(&tg_callbacks_mutex);
		free(clbks_tmp);
		return ERR_CLBK_REMOVE_THEREISNT;
	}
	return ERR_CLBK_REMOVE_THEREISNT;
}

int tg_callback_get(char * command, tg_callback_t * callback)
{
	int found = ERR_CLBK_GET;
	pthread_mutex_lock(&tg_callbacks_mutex);
	for (int i = 0; i < tg_callbacks_i; i++)
		if (!strcmp(tg_callbacks[i].command, command))
		{
			*callback = tg_callbacks[i];
			found = SUCCESS;
			break;
		}
	pthread_mutex_unlock(&tg_callbacks_mutex);
	return found;
}

int tg_callbacks_init()
{
	tg_callbacks_i = 0;
	return SUCCESS;
}

int tg_get_command(char * str4ka, char * command)
{
	char * sprt = " ";
	char str4ka_tmp[TG_MAX_MSG_LENGTH];
	strcpy(str4ka_tmp, str4ka);
	strcpy(command, &strtok(str4ka_tmp, sprt)[1]);
	return SUCCESS;
}

int tg_get_command_arg(char * str4ka, char * args, size_t args_size)
{
	char * sprt = " ";
	char * result = strstr(str4ka, sprt);
	if (result == NULL)
		return 1;
	snprintf(args, args_size, "%s", result+1);
	return SUCCESS;
}

int tg_queue_pop(tg_message_t ** task)
{
	while (1)
		if (pthread_mutex_lock(&tg_content_mutex) == 0)
		{
			if (tasks_queue_i)
			{
				pthread_mutex_unlock(&tg_content_mutex);
				return tg_queue_try_pop(task);
			}
			else
			{
				pthread_mutex_unlock(&tg_content_mutex);
				usleep(TG_INTERVAL);
			}
		}
}

int tg_queue_try_pop(tg_message_t ** task)
{
	if (tasks_queue_i == 0) return WARN_QUEUE_EMPTY;
	if (pthread_mutex_lock(&tg_content_mutex) == 0)
	{
		*task = (tg_message_t *)malloc(sizeof(tg_message_t));
		**task = tasks_queue[0];
		if (tasks_queue_i-- == 1)
		{
			free(tasks_queue);
			pthread_mutex_unlock(&tg_content_mutex);
			return 0;
		}
		tg_message_t * tasks_queue_tmp;
		tasks_queue_tmp = (tg_message_t *)
		                  malloc(tasks_queue_i * sizeof(tg_message_t));
		for (int i = 0; i < tasks_queue_i; i++)
		{
			tasks_queue_tmp[i] = tasks_queue[i + 1];
		}
		free(tasks_queue);
		tasks_queue = tasks_queue_tmp;
		pthread_mutex_unlock(&tg_content_mutex);
		return 0;
	}
	return 1;
}

int tg_queue_put(tg_message_t * task)
{
	if (pthread_mutex_lock(&tg_content_mutex) == 0)
	{
		tasks_queue = (tg_message_t *)realloc(tasks_queue,
		                                    ++tasks_queue_i * sizeof(tg_message_t));
		tasks_queue[tasks_queue_i - 1] = *task;
		pthread_mutex_unlock(&tg_content_mutex);
		return tasks_queue[tasks_queue_i - 1].chat_id ? 0 : 1;
	}
	return 1;
}

int tg_queue_init()
{
	tasks_queue_i = 0;
	return 0;
}

tg_message_t * tg_message_init()
{
	tg_message_t * msg = (tg_message_t *)malloc(sizeof(tg_message_t));
	memset(msg, 0, sizeof(tg_message_t));
	return msg;
}

int tg_get_content(json_object ** content_json)
{
	CURL * curl;
	tg_write_buf_t content_str;
	content_str.used = 0;

	char * TG_link = malloc(LINK_SIZE);
	char * TG_link_updates = malloc(LINK_SIZE);

	sprintf(TG_link, "%s%s/", TG_BASIC_LINK, tg_token);
	sprintf(TG_link_updates, "%s%s", TG_link, TG_METHOD_UPD);

	curl = curl_easy_init();
	if(curl)
	{
		curl_easy_setopt(curl, CURLOPT_URL, TG_link_updates);
		if (*tg_proxy)
			curl_easy_setopt(curl, CURLOPT_PROXY, tg_proxy);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, tg_curl_write);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &content_str);
	}

	CURLcode res;

	res = curl_easy_perform(curl);
	if(res != CURLE_OK)
	{
		fprintf(stderr, "curl_easy_perform() failed: %s\n",
		curl_easy_strerror(res));
	}
	else
		if (tg_work_on_answer(content_json, content_str.buf))
			printf("somthn wrong with content\n");

	curl_easy_cleanup(curl);

	free(TG_link);
	free(TG_link_updates);
	return 0;
}

int tg_drop_messages(int update_id)
{
	CURL * curl;
	tg_write_buf_t content_str;
	content_str.used = 0;

	char * TG_link = malloc(LINK_SIZE);
	char * TG_link_updates = malloc(LINK_SIZE);

	sprintf(TG_link, "%s%s/", TG_BASIC_LINK, tg_token);
	sprintf(TG_link_updates, "%s%s%s%d", TG_link, TG_METHOD_UPD,
	                                    "?offset=", ++update_id);

	curl = curl_easy_init();
	if(curl)
	{
		curl_easy_setopt(curl, CURLOPT_URL, TG_link_updates);
		if (*tg_proxy)
			curl_easy_setopt(curl, CURLOPT_PROXY, tg_proxy);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, tg_curl_write);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &content_str);
	}

	CURLcode res;

	res = curl_easy_perform(curl);
	if(res != CURLE_OK)
	{
		fprintf(stderr, "curl_easy_perform() failed: %s\n",
		curl_easy_strerror(res));
	}

	curl_easy_cleanup(curl);

	free(TG_link);
	free(TG_link_updates);
	return 0;
}

int tg_work_on_answer(json_object ** content_json, char * content_str)
{
	json_object * parsed = json_tokener_parse(content_str);
	if (!parsed)
		return ERR_ANSWR;

	json_object_put(*content_json);
	*content_json = parsed;

	if (!tg_content_isOk(*content_json))
		return SUCCESS;
	return ERR_ANSWR;
}

int tg_content_isOk(json_object * content_json)
{
	json_object * content_ok;
	json_object_object_get_ex(content_json, "ok", &content_ok);

	if (json_object_get_type(content_ok) == json_type_boolean)
	{
		if (json_object_get_boolean(content_ok))
			return SUCCESS;
	}
	return ERR_CONTENT_ISNTOK;
}

size_t tg_curl_write(void * ptr, size_t size, size_t nmemb, void * stream)
{
	tg_write_buf_t * buf = (tg_write_buf_t *)stream;
	size_t len = size * nmemb;

	/* Append the chunk: a response may arrive in several callbacks.
	 * On overflow the answer stays incomplete and will be rejected
	 * by the JSON parser instead of corrupting memory. */
	size_t room = BUFF_SIZE - 1 - buf->used;
	if (room > 0)
	{
		if (len > room) len = room;
		memcpy(buf->buf + buf->used, ptr, len);
		buf->used += len;
		buf->buf[buf->used] = '\0';
	}

	return size * nmemb;
}
