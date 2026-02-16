#include "parser.h"
#include <stdio.h>
#include <string.h>
#include <yaml.h>

/* TODO: Full YAML parsing will be implemented in phase1-yaml.
 * This is a stub that demonstrates the parser interface. */

int vmgen_parse_spec(const char *path, vmgen_spec_t *spec) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "vmgen: cannot open spec file '%s'\n", path);
        return -1;
    }

    yaml_parser_t parser;
    if (!yaml_parser_initialize(&parser)) {
        fprintf(stderr, "vmgen: failed to initialize YAML parser\n");
        fclose(f);
        return -1;
    }

    yaml_parser_set_input_file(&parser, f);

    /* TODO: Walk YAML events and populate spec */
    (void)spec;

    yaml_parser_delete(&parser);
    fclose(f);
    return 0;
}
