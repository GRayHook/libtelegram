#include "telega.h"

int main(int argc, char const *argv[]) {
	json_object *content_json;

	tg_start(&content_json);
	tg_callback_bind((char *)"doroy", &testiwe);
	sleep(100);

	return SUCCESS;
}

int testiwe(tg_message_t *msg)
{
	tg_message_t answer;
	if (tg_get_command_arg(msg->text, answer.text))
		strcpy(answer.text, "Dorooooyy");
	answer.chat_id = msg->chat_id;
	tg_send_message(&answer);

	return 0;
}
