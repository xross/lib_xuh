typedef struct
{
  int signature;
  int tag;
  unsigned length;
  char flags;
  char lun;
  char cblen;
  char cb[16];
} s_ms_cbw;
