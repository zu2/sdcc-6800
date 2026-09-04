typedef enum
  {
    SUB_MC6800,
  }
MC6800_SUB_PORT;

typedef struct
  {
    MC6800_SUB_PORT sub;
  }
MC6800_OPTS;

extern MC6800_OPTS mc6800_opts;

#define IS_MC6800 (mc6800_opts.sub == SUB_MC6800)
