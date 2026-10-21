/*******************************************************************************
                       Some intresting from 46 raw
*******************************************************************************/
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <pthread.h>

#define TG_BASIC_LINK "https://api.telegram.org/bot"
#define TG_METHOD_UPD "getUpdates"
#define TG_METHOD_SND "sendMessage"
#define TG_METHOD_SND_CHAT_ID "chat_id="
#define TG_METHOD_SND_TEXT "parse_mode=HTML&text="
#define BUFF_SIZE 256 * 1000
#define LINK_SIZE 8096
#define TG_INTERVAL 1000
#define TG_MAX_MSG_LENGTH 1024
#define TG_MSG_REGULAR 1
#define TG_MSG_COMMAND 2

#define SUCCESS 0
#define ERR_CONTENT_ISNTOK 10
#define ERR_ANSWR 20
#define ERR_CLBK_GET 30
#define ERR_CLBK_BIND 31
#define ERR_CLBK_REMOVE 40
#define ERR_CLBK_REMOVE_THEREISNT 41
#define ERR_TRY_CLBK 50
#define ERR_TRY_CLBK_NOT_CMD 51
#define WARN_QUEUE_EMPTY 60

/* Accumulating buffer for curl write callback */
typedef struct tg_write_buf_struct {
	char buf[BUFF_SIZE];
	size_t used;
} tg_write_buf_t;

typedef struct tg_message_struct {
	char type;
	int message_id;
	int64_t chat_id; // telegram ids are signed 64-bit (groups: -100...)
	char text[TG_MAX_MSG_LENGTH];
} tg_message_t;

typedef struct tg_callback_struct {
	char command[TG_MAX_MSG_LENGTH];
	int (*func)(tg_message_t *);
} tg_callback_t;

typedef struct tg_clbk_thread_arg_struct {
	int (*func)(tg_message_t *);
	tg_message_t * msg;
} tg_clbk_thread_arg_t;

/* Set proxy string, that will be gived to CURL */
int tg_set_proxy(const char * proxy);

/* Start thread that will fetching messages from telegram.
 * content_json - it will contains returned result in json. In future i plan
 * remove it, it must be replaced by abstraction features.
 * token - Your telegram token.
 * Return 0 if success */
int tg_start(json_object **content_json, char *token);

/* Funcs allow you to simple parsing your messages.
 * str4ka - message text
 * commmand - here will be result
 * args/args_size - destination buffer and its size, arg is truncated to fit
 * It return 0 when parsing success */
int tg_get_command(char *str4ka, char *command);
int tg_get_command_arg(char *str4ka, char *args, size_t args_size);

/* Send message.
 * msg - message struct with chat_id and text. It will be sended to chat_id
 * Return 0 if success */
int tg_send_message(tg_message_t *msg);

/* ASsign and remove your callback for command
 * command - command text
 * callback - place to copy the found callback into
 * Callback prototype:
 int tg_callback_proto(tg_message_t *msg);
 * Return 0 if success */
int tg_callback_bind(char *command, int (*callback_func)());
int tg_callback_remove(char *command);
int tg_callback_get(char *command, tg_callback_t *callback);

/* Manipulate with queue
* tg_queue_pop difference from tg_queue_try_pop in that it waits if queue is
 * empty. tg_queue_try_pop returns immediately. If queue is empty than
 * tg_queue_try_pop return WARN_QUEUE_EMPTY.
 * task - message
 * Usually you will haven't use tg_queue_put. But... */
int tg_queue_try_pop(tg_message_t **task);
int tg_queue_pop(tg_message_t **task);
int tg_queue_put(tg_message_t *task);

int tg_try_callback(tg_message_t *msg);
int tg_callbacks_init();
void *tg_circle_handler(void *args);
void *tg_clbk_thread_wrapper(void *arg);
int tg_queue_init();
tg_message_t *tg_message_init();
int tg_get_content(json_object **content_json);
int tg_drop_messages(int update_id);
int tg_work_on_answer(json_object **content_json, char *content_str);
int tg_content_isOk(json_object *content_json);
size_t tg_curl_write( void *ptr, size_t size, size_t nmemb, void *stream);
