#include "telega.h"

int main(int argc, char const *argv[]) {
	json_object *content_json;
	// tg_get_content(&content_json);
	tg_start(&content_json);
	sleep(3);
	tg_message_t *task = tg_message_init();
	tg_queue_pop(&task);
	printf("IN MAIN FROM POP: %s\n", task->text);
	tg_send_message(task);
	sleep(2);
	// if (tg_content_isOk(content_json))
	// 	return ERR_CONTENT_ISNTOK;

	return SUCCESS;
}
