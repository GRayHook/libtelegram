#include "tgcommon.h"

pthread_mutex_t tg_content_mutex = PTHREAD_MUTEX_INITIALIZER;
tg_message_t *tasks_queue;
int tasks_queue_i;

int tg_start(json_object **content_json) {
	pthread_t tg_get_content_thread;
	pthread_create(&tg_get_content_thread, NULL, tg_circle_handler, content_json);
	pthread_detach(tg_get_content_thread);
	return SUCCESS;
}

void *tg_circle_handler(void *arg) {
	json_object **content_json = (json_object **)arg;
	tg_queue_init();
	while (1) {
			tg_get_content(content_json);

			json_object *result_json;
			json_object_object_get_ex(*content_json, "result", &result_json);
			int messages_len = json_object_array_length(result_json);
			// TODO: Make arr sorted
			int update_id = 0;
			for (int i = 0; i < messages_len; i++) {
				json_object *message_json;
				message_json = json_object_array_get_idx(result_json, i);
				json_object *update_id_json;
				json_object_object_get_ex(message_json, "update_id", &update_id_json);
				update_id = json_object_get_int(update_id_json);
				printf("WORK ON %d\n", update_id);
				json_object *message_content_json;
				json_object_object_get_ex(message_json,
				                          "message",
				                          &message_content_json);
				json_object *message_id_json;
				json_object_object_get_ex(message_content_json,
				                          "message_id",
				                          &message_id_json);

				tg_message_t *cur_msg;
				cur_msg = (tg_message_t *)malloc(sizeof(tg_message_t));

				cur_msg->message_id = json_object_get_int(message_id_json);
				printf("message id is %d\n", cur_msg->message_id);

				json_object *chat_json;
				json_object_object_get_ex(message_content_json, "chat", &chat_json);
				json_object *chat_id_json;
				json_object_object_get_ex(chat_json, "id", &chat_id_json);
				cur_msg->chat_id = json_object_get_int(chat_id_json);
				printf("chat id is %d\n", cur_msg->chat_id);

				json_object *text_json;
				json_object_object_get_ex(message_content_json, "text", &text_json);
				strcpy(cur_msg->text, json_object_get_string(text_json));
				printf("text is %s\n", cur_msg->text);

				tg_queue_put(cur_msg);
				free(cur_msg);
			}
			tg_drop_messages(update_id);

			// printf("FROM QUEUE %s\n", tasks_queue[0].text);
			// printf("FROM QUEUE %s\n", tasks_queue[1].text);
			// tg_message_t *taskiwe;
			// tg_queue_pop(&taskiwe);
			// printf("FROM POP %s\n", taskiwe->text);

			sleep(TG_INTERVAL);
	}
}

int tg_queue_pop(tg_message_t **task)
{
	printf("%s\n", __func__);
	if (tasks_queue_i == 0) return ERR_QUEUE_POP;
	if (pthread_mutex_lock(&tg_content_mutex) == 0){
		*task = (tg_message_t *)malloc(sizeof(tg_message_t));
		memset(*task, 0, sizeof(tg_message_t));
		**task = tasks_queue[0];
		if (tasks_queue_i-- == 1) {
			free(tasks_queue);
			pthread_mutex_unlock(&tg_content_mutex);
			return 0;
		}
		tg_message_t *tasks_queue_tmp;
		tasks_queue_tmp = (tg_message_t *)
		                  malloc(tasks_queue_i * sizeof(tg_message_t));
		for (int i = 0; i < tasks_queue_i; i++) {
			tasks_queue_tmp[i] = tasks_queue[i + 1];
		}
		free(tasks_queue);
		tasks_queue = tasks_queue_tmp;
		pthread_mutex_unlock(&tg_content_mutex);
		return 0;
	}
	return ERR_QUEUE_POP;
}

int tg_queue_put(tg_message_t *task)
{
	printf("%s\n", __func__);
	if (pthread_mutex_lock(&tg_content_mutex) == 0)
	{
		printf("%s before realloc\n", __func__);
		tasks_queue = (tg_message_t *)realloc(tasks_queue,
		                                    ++tasks_queue_i * sizeof(tg_message_t));
		printf("%s after realloc\n", __func__);
		tasks_queue[tasks_queue_i - 1] = *task;
		printf("%s after =\n", __func__);
		pthread_mutex_unlock(&tg_content_mutex);
		printf("%s end\n", __func__);
		return (tasks_queue[tasks_queue_i - 1].message_id == task->message_id)
		       ? 0 : ERR_QUEUE_PUT;
	}
	return ERR_QUEUE_PUT;
}

int tg_queue_init()
{
	tasks_queue_i = 0;
	return 0;
}

/*
 *  Get updates from telegram by https
 */
int tg_get_content(json_object **content_json)
{
	CURL *curl;
	char content_str[BUFF_SIZE];

	char TG_link[LINK_SIZE];
	char TG_link_updates[LINK_SIZE];

	curl_global_init(CURL_GLOBAL_DEFAULT);

	sprintf(TG_link, "%s%s/", TG_BASIC_LINK, TG_TOKEN);
	sprintf(TG_link_updates, "%s%s", TG_link, TG_METHOD_UPD);

	curl = curl_easy_init();
	if(curl)
	{
		curl_easy_setopt(curl, CURLOPT_URL, TG_link_updates);
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
		if (tg_work_on_answer(content_json, content_str))
			printf("somthn wrong with content\n");

	curl_easy_cleanup(curl);
	curl_global_cleanup();

	return 0;
}

int tg_drop_messages(int update_id)
{
	CURL *curl;
	char content_str[BUFF_SIZE];

	char TG_link[LINK_SIZE];
	char TG_link_updates[LINK_SIZE];

	curl_global_init(CURL_GLOBAL_DEFAULT);

	sprintf(TG_link, "%s%s/", TG_BASIC_LINK, TG_TOKEN);
	sprintf(TG_link_updates, "%s%s%s%d", TG_link, TG_METHOD_UPD,
	                                    "?offset=", ++update_id);

	curl = curl_easy_init();
	if(curl)
	{
		curl_easy_setopt(curl, CURLOPT_URL, TG_link_updates);
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
	curl_global_cleanup();

	return 0;
}

int tg_work_on_answer(json_object **content_json, char *content_str)
{
	printf("result: %s\n", content_str);

	*content_json = json_tokener_parse(content_str);

	if (!tg_content_isOk(*content_json))
		return SUCCESS;
	return ERR_ANSWR;
}

int tg_content_isOk(json_object *content_json)
{
	json_object *content_ok;
	json_object_object_get_ex(content_json, "ok", &content_ok);

	if (json_object_get_type(content_ok) == json_type_boolean)
	{
		if (TRUE == json_object_get_boolean(content_ok))
			return SUCCESS;
	}
	return ERR_CONTENT_ISNTOK;
}

size_t tg_curl_write( void *ptr, size_t size, size_t nmemb, void *stream)
{
	return sprintf(stream, "%s", (char *)ptr);
}
