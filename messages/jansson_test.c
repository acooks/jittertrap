#include <jansson.h>

int main()
{
	json_t *tok;
	json_error_t err;

	const char *s = "{}";
	tok = json_loads(s, 0, &err);

	if (!tok) {
		fprintf(stderr, "error loading string: %s\n",s);
	}
	json_decref(tok);
	return 0;
}
