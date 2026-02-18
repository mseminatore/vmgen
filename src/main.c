#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "spec/model.h"
#include "spec/parser.h"
#include "spec/parser_toml.h"
#include "spec/validate.h"
#include "codegen/emit.h"
#include "codegen/gen_opcodes.h"

#ifdef _WIN32
#include <direct.h>
#define MKDIRP(d) _mkdir(d)
#else
#include <sys/stat.h>
#define MKDIRP(d) mkdir(d, 0755)
#endif
#include "codegen/gen_vm.h"
#include "codegen/gen_assembler.h"
#include "codegen/gen_disasm.h"
#include "codegen/gen_gc.h"
#include "codegen/gen_strings.h"
#include "codegen/gen_closures.h"
#include "codegen/gen_modules.h"
#include "codegen/gen_fibers.h"
#include "codegen/gen_native.h"
#include "codegen/gen_build.h"
#include "codegen/gen_bytecode.h"
#include "codegen/gen_test.h"
#include "codegen/gen_linemap.h"
#include "codegen/gen_verify.h"
#include "codegen/gen_single_header.h"

static void print_usage(const char *progname) {
    fprintf(stderr, "Usage: %s <spec.yaml> -o <output_dir>\n", progname);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -o <dir>    Output directory for generated C code\n");
    fprintf(stderr, "  -h          Show this help message\n");
}

int main(int argc, char *argv[]) {
    const char *spec_path = NULL;
    const char *output_dir = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -o requires an argument\n");
                return 1;
            }
            output_dir = argv[++i];
        } else if (argv[i][0] != '-') {
            spec_path = argv[i];
        } else {
            fprintf(stderr, "Error: unknown option '%s'\n", argv[i]);
            return 1;
        }
    }

    if (!spec_path) {
        fprintf(stderr, "Error: no spec file specified\n");
        print_usage(argv[0]);
        return 1;
    }

    if (!output_dir) {
        fprintf(stderr, "Error: no output directory specified (-o)\n");
        print_usage(argv[0]);
        return 1;
    }

    /* Parse the spec (detect format by file extension) */
    vmgen_spec_t spec;
    vmgen_spec_init(&spec);

    int rc;
    const char *ext = strrchr(spec_path, '.');
    if (ext && (strcmp(ext, ".toml") == 0 || strcmp(ext, ".TOML") == 0)) {
        rc = vmgen_parse_spec_toml(spec_path, &spec);
    } else {
        rc = vmgen_parse_spec(spec_path, &spec);
    }
    if (rc != 0) {
        fprintf(stderr, "Error: failed to parse spec '%s'\n", spec_path);
        vmgen_spec_free(&spec);
        return 1;
    }

    /* Validate the spec */
    rc = vmgen_validate_spec(&spec);
    if (rc != 0) {
        fprintf(stderr, "Error: spec validation failed\n");
        vmgen_spec_free(&spec);
        return 1;
    }

    printf("vmgen: generating VM '%s' v%s -> %s/\n",
           spec.name, spec.version, output_dir);

    /* Ensure output directory exists */
    MKDIRP(output_dir);

    /* Code generation */
    rc = vmgen_gen_opcodes(&spec, output_dir);
    if (rc != 0) {
        fprintf(stderr, "Error: opcode generation failed\n");
        vmgen_spec_free(&spec);
        return 1;
    }

    rc = vmgen_gen_vm(&spec, output_dir);
    if (rc != 0) {
        fprintf(stderr, "Error: VM generation failed\n");
        vmgen_spec_free(&spec);
        return 1;
    }

    rc = vmgen_gen_assembler(&spec, output_dir);
    if (rc != 0) {
        fprintf(stderr, "Error: assembler generation failed\n");
        vmgen_spec_free(&spec);
        return 1;
    }

    rc = vmgen_gen_disasm(&spec, output_dir);
    if (rc != 0) {
        fprintf(stderr, "Error: disassembler generation failed\n");
        vmgen_spec_free(&spec);
        return 1;
    }

    rc = vmgen_gen_strings(&spec, output_dir);
    if (rc != 0) {
        fprintf(stderr, "Error: string intern generation failed\n");
        vmgen_spec_free(&spec);
        return 1;
    }

    rc = vmgen_gen_gc(&spec, output_dir);
    if (rc != 0) {
        fprintf(stderr, "Error: GC generation failed\n");
        vmgen_spec_free(&spec);
        return 1;
    }

    rc = vmgen_gen_native(&spec, output_dir);
    if (rc != 0) {
        fprintf(stderr, "Error: native function generation failed\n");
        vmgen_spec_free(&spec);
        return 1;
    }

    rc = vmgen_gen_closures(&spec, output_dir);
    if (rc != 0) {
        fprintf(stderr, "Error: closure generation failed\n");
        vmgen_spec_free(&spec);
        return 1;
    }

    rc = vmgen_gen_modules(&spec, output_dir);
    if (rc != 0) {
        fprintf(stderr, "Error: module generation failed\n");
        vmgen_spec_free(&spec);
        return 1;
    }

    rc = vmgen_gen_fibers(&spec, output_dir);
    if (rc != 0) {
        fprintf(stderr, "Error: fiber generation failed\n");
        vmgen_spec_free(&spec);
        return 1;
    }

    rc = vmgen_gen_bytecode(&spec, output_dir);
    if (rc != 0) {
        fprintf(stderr, "Error: bytecode format generation failed\n");
        vmgen_spec_free(&spec);
        return 1;
    }

    rc = vmgen_gen_test(&spec, output_dir);
    if (rc != 0) {
        fprintf(stderr, "Error: test harness generation failed\n");
        vmgen_spec_free(&spec);
        return 1;
    }

    rc = vmgen_gen_linemap(&spec, output_dir);
    if (rc != 0) {
        fprintf(stderr, "Error: linemap generation failed\n");
        vmgen_spec_free(&spec);
        return 1;
    }

    rc = vmgen_gen_verify(&spec, output_dir);
    if (rc != 0) {
        fprintf(stderr, "Error: verifier generation failed\n");
        vmgen_spec_free(&spec);
        return 1;
    }

    rc = vmgen_gen_build(&spec, output_dir);
    if (rc != 0) {
        fprintf(stderr, "Error: build file generation failed\n");
        vmgen_spec_free(&spec);
        return 1;
    }

    /* Single-header amalgamation (must run after all other generators) */
    rc = vmgen_gen_single_header(&spec, output_dir);
    if (rc != 0) {
        fprintf(stderr, "Error: single-header generation failed\n");
        vmgen_spec_free(&spec);
        return 1;
    }

    vmgen_spec_free(&spec);
    printf("vmgen: done\n");
    return 0;
}
