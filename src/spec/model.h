#ifndef VMGEN_MODEL_H
#define VMGEN_MODEL_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Maximum limits */
#define VMGEN_MAX_NAME       64
#define VMGEN_MAX_VERSION    16
#define VMGEN_MAX_TYPES      16
#define VMGEN_MAX_INSTR     256
#define VMGEN_MAX_OPERANDS    4
#define VMGEN_MAX_CONSTANTS  16
#define VMGEN_MAX_DESC      128
#define VMGEN_MAX_ACTION   1024

/* Operand types */
typedef enum {
    VMGEN_OPERAND_UINT8,
    VMGEN_OPERAND_UINT16,
    VMGEN_OPERAND_UINT32,
    VMGEN_OPERAND_INT8,
    VMGEN_OPERAND_INT16,
    VMGEN_OPERAND_INT32,
} vmgen_operand_type_t;

/* A single operand definition */
typedef struct {
    char name[VMGEN_MAX_NAME];
    vmgen_operand_type_t type;
} vmgen_operand_t;

/* An instruction definition */
#define VMGEN_MAX_TYPED_ACTIONS 8

typedef struct {
    char name[VMGEN_MAX_NAME];
    char description[VMGEN_MAX_DESC];
    vmgen_operand_t operands[VMGEN_MAX_OPERANDS];
    int operand_count;
    char *action;                       /* stack-effect DSL string (heap, may be NULL) */
    char *action_c;                     /* inline C snippet (heap, may be NULL) */
    int stack_effect;                   /* declared net stack effect */
    bool stack_effect_set;              /* whether stack_effect was explicitly declared */

    /* Type-dispatched actions: per-type DSL strings + optional default (inline C) */
    char typed_action_types[VMGEN_MAX_TYPED_ACTIONS][VMGEN_MAX_NAME];
    char *typed_actions[VMGEN_MAX_TYPED_ACTIONS];   /* DSL strings (heap) */
    int typed_action_count;
    char *typed_action_default;                     /* inline C fallback (heap, may be NULL) */
} vmgen_instr_t;

/* Allocation strategy (shared by stack, constant pool, frames) */
typedef enum {
    VMGEN_ALLOC_FIXED,
    VMGEN_ALLOC_DYNAMIC,
} vmgen_alloc_type_t;

/* Stack configuration */
typedef enum {
    VMGEN_STACK_FIXED,
    VMGEN_STACK_DYNAMIC,
} vmgen_stack_type_t;

typedef enum {
    VMGEN_VALUE_TAGGED_UNION,
    VMGEN_VALUE_DOUBLE,
    VMGEN_VALUE_INT64,
} vmgen_value_type_t;

typedef struct {
    vmgen_stack_type_t type;
    int max_depth;
    vmgen_value_type_t value_type;
} vmgen_stack_config_t;

/* Constant pool configuration */
typedef struct {
    vmgen_alloc_type_t type;
    int initial_capacity;
} vmgen_pool_config_t;

/* Frame stack configuration */
typedef struct {
    vmgen_alloc_type_t type;
    int max_depth;
} vmgen_frame_config_t;

/* GC strategy */
typedef enum {
    VMGEN_GC_NONE,
    VMGEN_GC_MARK_SWEEP,
    VMGEN_GC_REFCOUNT,
} vmgen_gc_strategy_t;

/* Dispatch strategy */
typedef enum {
    VMGEN_DISPATCH_SWITCH,
    VMGEN_DISPATCH_FNPTR,
    VMGEN_DISPATCH_THREADED,
    VMGEN_DISPATCH_AUTO,
} vmgen_dispatch_t;

/* Module loading strategy */
typedef enum {
    VMGEN_MODULE_STATIC,
    VMGEN_MODULE_DYNAMIC,
    VMGEN_MODULE_BOTH,
} vmgen_module_loading_t;

/* VM type flags */
typedef enum {
    VMGEN_TYPE_INT64  = (1 << 0),
    VMGEN_TYPE_DOUBLE = (1 << 1),
    VMGEN_TYPE_BOOL   = (1 << 2),
    VMGEN_TYPE_STRING = (1 << 3),
    VMGEN_TYPE_OBJECT = (1 << 4),
} vmgen_type_flags_t;

/* User-defined value types (tagged-union stacks only).
 * Declared in the spec's `types` list as a mapping, e.g.
 *   types: [int64, object, { name: cell, payload: pointer }]
 * Each one adds <NAME>_VAL_<TYPE> to the value-type enum and a
 * <NAME>_<TYPE>(v) constructor macro that stores v in the chosen union field. */
#define VMGEN_MAX_CUSTOM_TYPES 16

typedef enum {
    VMGEN_PAYLOAD_POINTER,   /* stored in .as.obj (void *) */
    VMGEN_PAYLOAD_INT64,     /* stored in .as.i64 */
    VMGEN_PAYLOAD_DOUBLE,    /* stored in .as.f64 */
} vmgen_payload_t;

typedef struct {
    char name[VMGEN_MAX_NAME];          /* lower-case C identifier, e.g. "cell" */
    vmgen_payload_t payload;
    bool gc;                            /* pointer payload: mark as a GC root */
    char comment[VMGEN_MAX_DESC];       /* optional description */
} vmgen_custom_type_t;

/* Constant pool type flags */
typedef enum {
    VMGEN_CONST_INT64  = (1 << 0),
    VMGEN_CONST_DOUBLE = (1 << 1),
    VMGEN_CONST_STRING = (1 << 2),
} vmgen_const_flags_t;

/* VM struct extension field */
#define VMGEN_MAX_EXTENSIONS 16

typedef struct {
    char name[VMGEN_MAX_NAME];
    char type_str[VMGEN_MAX_DESC];      /* C type string, e.g. "void *" */
    char include[VMGEN_MAX_NAME];       /* optional header to #include */
    char comment[VMGEN_MAX_DESC];       /* optional inline comment */
} vmgen_extension_t;

/* Features configuration */
typedef struct {
    bool interned_strings;

    bool gc_enabled;
    vmgen_gc_strategy_t gc_strategy;

    bool closures;

    bool modules_enabled;
    vmgen_module_loading_t module_loading;

    bool coroutines;
    bool debug_tracing;
    bool nan_boxing;
    bool exception_handling;
    bool expose_dispatch_table;
} vmgen_features_t;

/* Top-level spec */
typedef struct {
    char name[VMGEN_MAX_NAME];
    char version[VMGEN_MAX_VERSION];

    vmgen_stack_config_t stack;
    vmgen_pool_config_t pool;
    vmgen_frame_config_t frame;
    uint32_t type_flags;       /* vmgen_type_flags_t bitmask */
    vmgen_custom_type_t custom_types[VMGEN_MAX_CUSTOM_TYPES];
    int custom_type_count;     /* entries beyond this are ignored */
    int custom_type_overflow;  /* set if the spec declared more than the max */
    vmgen_features_t features;
    vmgen_dispatch_t dispatch;

    vmgen_instr_t instructions[VMGEN_MAX_INSTR];
    int instruction_count;

    uint32_t const_flags;      /* vmgen_const_flags_t bitmask */

    vmgen_extension_t extensions[VMGEN_MAX_EXTENSIONS];
    int extension_count;
} vmgen_spec_t;

void vmgen_spec_init(vmgen_spec_t *spec);
void vmgen_spec_free(vmgen_spec_t *spec);

#endif /* VMGEN_MODEL_H */
