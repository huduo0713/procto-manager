#include "proto_mqtt.h"
#include <yaml.h>

// 简单安全复制字符串
static void copy_str(char *dst, size_t dst_size, const char *src) {
	if (!dst || dst_size == 0) return;
	if (!src) { dst[0] = '\0'; return; }
	strncpy(dst, src, dst_size - 1);
	dst[dst_size - 1] = '\0';
}

// 将 YAML 标量转为布尔（支持 true/false/1/0/yes/no）
static int parse_bool(const char *s, int default_val) {
	if (!s) return default_val;
	if (!strcasecmp(s, "true") || !strcasecmp(s, "yes") || !strcmp(s, "1")) return 1;
	if (!strcasecmp(s, "false") || !strcasecmp(s, "no") || !strcmp(s, "0")) return 0;
	return default_val;
}

int load_mqtt_config_from_yaml(const char* yaml_path, mqtt_config_t* cfg) {
	if (!yaml_path || !cfg) return -1;

	// 初始化默认值
	memset(cfg, 0, sizeof(*cfg));
	cfg->qos = 1;
	cfg->retained = 0;
	cfg->timeout_ms = DEFAULT_TIMEOUT_MS;
	cfg->keepalive_interval = 60;
	cfg->reconnect_interval = 5;
	cfg->max_reconnect_attempts = 0;
	cfg->enable_auto_reconnect = 1;

	FILE *fh = fopen(yaml_path, "rb");
	if (!fh) return -2;

	yaml_parser_t parser;
	if (!yaml_parser_initialize(&parser)) { fclose(fh); return -3; }
	yaml_parser_set_input_file(&parser, fh);

	enum Section { SEC_NONE, SEC_PROTOCOLS, SEC_MQTT, SEC_AUTH, SEC_SETTINGS, SEC_TOPICS };
	enum Section stack[8];
	int sp = 0; // 栈顶
	stack[sp] = SEC_NONE;

	char current_key[128] = {0};
	yaml_event_t event;
	int in_key = 0; // 最近一个标量是否是 key
	int rc = 0;

	while (yaml_parser_parse(&parser, &event)) {
		switch (event.type) {
			case YAML_STREAM_START_EVENT:
				break;
			case YAML_STREAM_END_EVENT:
				goto done;
			case YAML_DOCUMENT_START_EVENT:
				break;
			case YAML_DOCUMENT_END_EVENT:
				break;
			case YAML_MAPPING_START_EVENT:
				if (sp < 7) {
					sp++;
					// 继承父区块，默认不变
					stack[sp] = stack[sp-1];
					// 如果当前 key 指示新的区块，切换
					if (!strcasecmp(current_key, "protocols")) stack[sp] = SEC_PROTOCOLS;
					else if (!strcasecmp(current_key, "mqtt")) stack[sp] = SEC_MQTT;
					else if (!strcasecmp(current_key, "authentication")) stack[sp] = SEC_AUTH;
					else if (!strcasecmp(current_key, "settings")) stack[sp] = SEC_SETTINGS;
					else if (!strcasecmp(current_key, "topics")) stack[sp] = SEC_TOPICS;
					current_key[0] = '\0';
				}
				in_key = 1;
				break;
			case YAML_MAPPING_END_EVENT:
				if (sp > 0) sp--;
				in_key = 1;
				break;
			case YAML_SEQUENCE_START_EVENT:
				// 不期望序列，跳过
				in_key = 1;
				break;
			case YAML_SEQUENCE_END_EVENT:
				in_key = 1;
				break;
			case YAML_SCALAR_EVENT: {
				const char *val = (const char*)event.data.scalar.value;
				if (in_key) {
					copy_str(current_key, sizeof(current_key), val);
					in_key = 0;
				} else {
					// value
					// 仅在相关区块处理
					switch (stack[sp]) {
						case SEC_MQTT:
							if (!strcasecmp(current_key, "broker_uri")) copy_str(cfg->broker, sizeof(cfg->broker), val);
							else if (!strcasecmp(current_key, "client_id")) copy_str(cfg->client_id, sizeof(cfg->client_id), val);
							break;
						case SEC_AUTH:
							if (!strcasecmp(current_key, "username")) copy_str(cfg->username, sizeof(cfg->username), val);
							else if (!strcasecmp(current_key, "password")) copy_str(cfg->password, sizeof(cfg->password), val);
							break;
						case SEC_SETTINGS: {
							if (!strcasecmp(current_key, "keepalive_interval_sec")) cfg->keepalive_interval = (int)strtol(val, NULL, 10);
							else if (!strcasecmp(current_key, "default_qos")) cfg->qos = (int)strtol(val, NULL, 10);
							else if (!strcasecmp(current_key, "default_retained")) cfg->retained = parse_bool(val, 0);
							else if (!strcasecmp(current_key, "operation_timeout_ms")) cfg->timeout_ms = (int)strtol(val, NULL, 10);
							else if (!strcasecmp(current_key, "max_reconnect_attempts")) cfg->max_reconnect_attempts = (int)strtol(val, NULL, 10);
							else if (!strcasecmp(current_key, "enable_auto_reconnect")) cfg->enable_auto_reconnect = parse_bool(val, 1);
							else if (!strcasecmp(current_key, "reconnect_interval_sec")) cfg->reconnect_interval = (int)strtol(val, NULL, 10);
							break;
						}
						case SEC_TOPICS:
							if (!strcasecmp(current_key, "publish_topic")) copy_str(cfg->pub_topic, sizeof(cfg->pub_topic), val);
							else if (!strcasecmp(current_key, "subscribe_topic")) copy_str(cfg->sub_topic, sizeof(cfg->sub_topic), val);
							break;
						default:
							break;
					}
					current_key[0] = '\0';
					in_key = 1;
				}
				break;
			}
			default:
				break;
		}
		yaml_event_delete(&event);
	}
	// 解析失败
	rc = -4;
	done:
	yaml_event_delete(&event);
	yaml_parser_delete(&parser);
	fclose(fh);

	// 简单必填校验
	if (cfg->broker[0] == '\0' || cfg->client_id[0] == '\0') return -5;
	if (cfg->pub_topic[0] == '\0') copy_str(cfg->pub_topic, sizeof(cfg->pub_topic), "device/echo");
	if (cfg->sub_topic[0] == '\0') copy_str(cfg->sub_topic, sizeof(cfg->sub_topic), cfg->pub_topic);
	return rc;
}
