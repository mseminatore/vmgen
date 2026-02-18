#include "parser_toml.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <toml.h>

#ifdef _MSC_VER
#define vmgen_strdup _strdup
#else
#define vmgen_strdup strdup
#endif

static void safe_copy(char *dst, const char *src, size_t max) {
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, max - 1);
    dst[max - 1] = '\0';
}

static int str_eq(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

static vmgen_operand_type_t parse_operand_type(const char *s) {
    if (str_eq(s, "uint8"))  return VMGEN_OPERAND_UINT8;
    if (str_eq(s, "uint16")) return VMGEN_OPERAND_UINT16;
    if (str_eq(s, "uint32")) return VMGEN_OPERAND_UINT32;
    if (str_eq(s, "int8"))   return VMGEN_OPERAND_INT8;
    if (str_eq(s, "int16"))  return VMGEN_OPERAND_INT16;
    if (str_eq(s, "int32"))  return VMGEN_OPERAND_INT32;
    fprintf(stderr, "vmgen: unknown operand type '%s'\n", s);
    return VMGEN_OPERAND_UINT8;
}

static void parse_stack(toml_table_t *stack, vmgen_spec_t *spec) {
    toml_datum_t d;

    d = toml_string_in(stack, "type");
    if (d.ok) {
        if (str_eq(d.u.s, "dynamic")) spec->stack.type = VMGEN_STACK_DYNAMIC;
        else spec->stack.type = VMGEN_STACK_FIXED;
        free(d.u.s);
    }

    d = toml_int_in(stack, "max_depth");
    if (d.ok) spec->stack.max_depth = (int)d.u.i;

    d = toml_string_in(stack, "value_type");
    if (d.ok) {
        if (str_eq(d.u.s, "double")) spec->stack.value_type = VMGEN_VALUE_DOUBLE;
        else if (str_eq(d.u.s, "int64")) spec->stack.value_type = VMGEN_VALUE_INT64;
        else spec->stack.value_type = VMGEN_VALUE_TAGGED_UNION;
        free(d.u.s);
    }
}

static void parse_types(toml_array_t *arr, vmgen_spec_t *spec) {
    int n = toml_array_nelem(arr);
    for (int i = 0; i < n; i++) {
        toml_datum_t d = toml_string_at(arr, i);
        if (!d.ok) continue;
        if (str_eq(d.u.s, "int64"))  spec->type_flags |= VMGEN_TYPE_INT64;
        else if (str_eq(d.u.s, "double")) spec->type_flags |= VMGEN_TYPE_DOUBLE;
        else if (str_eq(d.u.s, "bool"))   spec->type_flags |= VMGEN_TYPE_BOOL;
        else if (str_eq(d.u.s, "string")) spec->type_flags |= VMGEN_TYPE_STRING;
        else if (str_eq(d.u.s, "object")) spec->type_flags |= VMGEN_TYPE_OBJECT;
        free(d.u.s);
    }
}

static void parse_features(toml_table_t *feat, vmgen_spec_t *spec) {
    toml_datum_t d;

    d = toml_bool_in(feat, "interned_strings");
    if (d.ok) spec->features.interned_strings = d.u.b;

    /* garbage_collection sub-table */
    toml_table_t *gc = toml_table_in(feat, "garbage_collection");
    if (gc) {
        d = toml_bool_in(gc, "enabled");
        if (d.ok) spec->features.gc_enabled = d.u.b;
        d = toml_string_in(gc, "strategy");
        if (d.ok) {
            if (str_eq(d.u.s, "refcount")) spec->features.gc_strategy = VMGEN_GC_REFCOUNT;
            else spec->features.gc_strategy = VMGEN_GC_MARK_SWEEP;
            free(d.u.s);
        }
    }

    d = toml_bool_in(feat, "closures");
    if (d.ok) spec->features.closures = d.u.b;

    /* modules sub-table */
    toml_table_t *mod = toml_table_in(feat, "modules");
    if (mod) {
        d = toml_bool_in(mod, "enabled");
        if (d.ok) spec->features.modules_enabled = d.u.b;
        d = toml_string_in(mod, "loading");
        if (d.ok) {
            if (str_eq(d.u.s, "static")) spec->features.module_loading = VMGEN_MODULE_STATIC;
            else if (str_eq(d.u.s, "both")) spec->features.module_loading = VMGEN_MODULE_BOTH;
            else spec->features.module_loading = VMGEN_MODULE_DYNAMIC;
            free(d.u.s);
        }
    }

    d = toml_bool_in(feat, "coroutines");
    if (d.ok) spec->features.coroutines = d.u.b;

    d = toml_bool_in(feat, "debug_tracing");
    if (d.ok) spec->features.debug_tracing = d.u.b;

    d = toml_bool_in(feat, "nan_boxing");
    if (d.ok) spec->features.nan_boxing = d.u.b;
}

static void parse_operands(toml_array_t *arr, vmgen_instr_t *instr) {
    int n = toml_array_nelem(arr);
    if (n > VMGEN_MAX_OPERANDS) n = VMGEN_MAX_OPERANDS;
    for (int i = 0; i < n; i++) {
        toml_table_t *op = toml_table_at(arr, i);
        if (!op) continue;
        toml_datum_t d = toml_string_in(op, "name");
        if (d.ok) {
            safe_copy(instr->operands[i].name, d.u.s, VMGEN_MAX_NAME);
            free(d.u.s);
        }
        d = toml_string_in(op, "type");
        if (d.ok) {
            instr->operands[i].type = parse_operand_type(d.u.s);
            free(d.u.s);
        }
        instr->operand_count++;
    }
}

static void parse_instructions(toml_array_t *arr, vmgen_spec_t *spec) {
    int n = toml_array_nelem(arr);
    if (n > VMGEN_MAX_INSTR) n = VMGEN_MAX_INSTR;
    for (int i = 0; i < n; i++) {
        toml_table_t *instr_tbl = toml_table_at(arr, i);
        if (!instr_tbl) continue;

        vmgen_instr_t *instr = &spec->instructions[spec->instruction_count];
        toml_datum_t d;

        d = toml_string_in(instr_tbl, "name");
        if (d.ok) { safe_copy(instr->name, d.u.s, VMGEN_MAX_NAME); free(d.u.s); }

        d = toml_string_in(instr_tbl, "description");
        if (d.ok) { safe_copy(instr->description, d.u.s, VMGEN_MAX_DESC); free(d.u.s); }

        d = toml_string_in(instr_tbl, "action");
        if (d.ok) { instr->action = vmgen_strdup(d.u.s); free(d.u.s); }

        d = toml_string_in(instr_tbl, "action_c");
        if (d.ok) { instr->action_c = vmgen_strdup(d.u.s); free(d.u.s); }

        d = toml_int_in(instr_tbl, "stack_effect");
        if (d.ok) { instr->stack_effect = (int)d.u.i; instr->stack_effect_set = true; }

        toml_array_t *ops = toml_array_in(instr_tbl, "operands");
        if (ops) parse_operands(ops, instr);

        spec->instruction_count++;
    }
}

static void parse_constants(toml_array_t *arr, vmgen_spec_t *spec) {
    int n = toml_array_nelem(arr);
    for (int i = 0; i < n; i++) {
        toml_datum_t d = toml_string_at(arr, i);
        if (!d.ok) continue;
        if (str_eq(d.u.s, "int64"))  spec->const_flags |= VMGEN_CONST_INT64;
        else if (str_eq(d.u.s, "double")) spec->const_flags |= VMGEN_CONST_DOUBLE;
        else if (str_eq(d.u.s, "string")) spec->const_flags |= VMGEN_CONST_STRING;
        free(d.u.s);
    }
}

int vmgen_parse_spec_toml(const char *path, vmgen_spec_t *spec) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "vmgen: cannot open '%s'\n", path);
        return -1;
    }

    char errbuf[256];
    toml_table_t *root = toml_parse_file(fp, errbuf, sizeof(errbuf));
    fclose(fp);

    if (!root) {
        fprintf(stderr, "vmgen: TOML parse error: %s\n", errbuf);
        return -1;
    }

    /* Top-level [vm] table */
    toml_table_t *vm = toml_table_in(root, "vm");
    if (!vm) {
        fprintf(stderr, "vmgen: missing [vm] table in TOML spec\n");
        toml_free(root);
        return -1;
    }

    toml_datum_t d;
    d = toml_string_in(vm, "name");
    if (d.ok) { safe_copy(spec->name, d.u.s, VMGEN_MAX_NAME); free(d.u.s); }

    d = toml_string_in(vm, "version");
    if (d.ok) { safe_copy(spec->version, d.u.s, VMGEN_MAX_VERSION); free(d.u.s); }

    /* Stack */
    toml_table_t *stack = toml_table_in(vm, "stack");
    if (stack) parse_stack(stack, spec);

    /* Types */
    toml_array_t *types = toml_array_in(vm, "types");
    if (types) parse_types(types, spec);

    /* Features */
    toml_table_t *feat = toml_table_in(vm, "features");
    if (feat) parse_features(feat, spec);

    /* Dispatch */
    d = toml_string_in(vm, "dispatch");
    if (d.ok) {
        if (str_eq(d.u.s, "switch")) spec->dispatch = VMGEN_DISPATCH_SWITCH;
        else if (str_eq(d.u.s, "fnptr")) spec->dispatch = VMGEN_DISPATCH_FNPTR;
        else if (str_eq(d.u.s, "threaded")) spec->dispatch = VMGEN_DISPATCH_THREADED;
        else spec->dispatch = VMGEN_DISPATCH_AUTO;
        free(d.u.s);
    }

    /* Instructions */
    toml_array_t *instrs = toml_array_in(vm, "instructions");
    if (instrs) parse_instructions(instrs, spec);

    /* Constants */
    toml_array_t *consts = toml_array_in(vm, "constants");
    if (consts) parse_constants(consts, spec);

    toml_free(root);
    return 0;
}
