#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <pthread.h>

#define TG_TOKEN "513932073:AAFFWtkfEFnW0WrVjVbjqR20HclSC670K7E"
#define TG_BASIC_LINK "https://api.telegram.org/bot"
#define TG_METHOD_UPD "getUpdates"
#define TG_METHOD_SND "sendMessage"
#define TG_METHOD_SND_CHAT_ID "chat_id="
#define TG_METHOD_SND_TEXT "text="
#define BUFF_SIZE 256 * 1000
#define LINK_SIZE 1536
#define TG_INTERVAL 1000
#define TG_MAX_MSG_LENGTH 1024
#define TG_MSG_REGULAR 1
#define TG_MSG_COMMAND 2

#define SUCCESS 0
#define ERR_CONTENT_ISNTOK 10
#define ERR_ANSWR 20
#define ERR_CLBK_GET 30
#define ERR_CLBK_REMOVE 40
#define ERR_CLBK_REMOVE_THEREISNT 41
#define ERR_TRY_CLBK 50
#define ERR_TRY_CLBK_NOT_CMD 51

typedef struct tg_message_struct {
	char type;
	int message_id;
	unsigned int chat_id;
	char text[TG_MAX_MSG_LENGTH];
} tg_message_t;

typedef struct tg_callback_struct {
	char command[TG_MAX_MSG_LENGTH];
	int (*func)(tg_message_t *);
} tg_callback_t;

int tg_callback_proto(tg_message_t *msg);
int tg_try_callback(tg_message_t *msg);
int tg_callback_remove(char *command);
int tg_callback_bind(char *command, int (*callback_func)());
int tg_callback_get(char *command, tg_callback_t **callback);
int tg_callbacks_init();
int tg_get_command(char *str4ka, char *command);
int tg_get_command_arg(char *str4ka, char *args);
int tg_start(json_object **content_json);
void *tg_circle_handler(void *args);
int tg_send_message(tg_message_t *msg);
int tg_queue_try_pop(tg_message_t **task);
int tg_queue_pop(tg_message_t **task);
int tg_queue_put(tg_message_t *task);
int tg_queue_init();
tg_message_t *tg_message_init();
int tg_get_content(json_object **content_json);
int tg_drop_messages(int update_id);
int tg_work_on_answer(json_object **content_json, char *content_str);
int tg_content_isOk(json_object *content_json);
size_t tg_curl_write( void *ptr, size_t size, size_t nmemb, void *stream);
