#ifndef SB_SEARCH_H
#define SB_SEARCH_H
#include "core/app.h"
extern Screen searchScreen;
/* The Search keyboard: QWERTY, staggered as on a real keyboard (Q row, A row half a key in, Z row a key in), the digits
   as a row after the letters, then a row of Space, Backspace and Clear. Key i of SEARCH_KEYS, or SEARCH_NK + 0/1/2 for
   that last row. The microphone (SEARCH_NK + 3, where the system has a speech recognizer) sits right of the query box,
   a row above the keys (row -1). The tour's picture of Search draws the same keys. */
#ifndef SB_PHONE_TYPE
#define SB_PHONE_TYPE 1 /* Search offers typing on a phone (its QR); target.h: 0 where a keyboard types */
#endif
#define SEARCH_KEYS "qwertyuiopasdfghjklzxcvbnm1234567890"
#define SEARCH_NK 36
#define SEARCH_KROWS 4 /* rows of characters; row SEARCH_KROWS is Space, Backspace, Clear */
#define SEARCH_KPITCH 55.6f /* 10 keys of 50 fill the 552 wide panel */
#define SEARCH_KW 50.0f
#define SEARCH_MICW 66.0f /* the microphone beside the query box, and the gap before it */
#define SEARCH_MICGAP 10.0f
#define SEARCH_KROWH 74.0f /* a row's step; a key is 66 high */
int search_key_row_len(int r); /* keys in row r (3 in the last; row -1 is the microphone alone) */
int search_key_index(int r, int c); /* the key at row r, place c */
void search_key_rc(int ki, int *r, int *c);
void search_key_pos(int ki, float *x, float *y, float *w); /* from the keyboard's top left, left to right */
/* phones (screens/phone_search.c) */
const char *search_query(void);
void search_set_query(const char *q);
const ItemList *search_results(void);
int search_busy(void);
int search_sug_list(const char **out, int max);
void search_now(void); /* Enter: ask at once, without waiting for the typing to pause */
void search_type_badge(const char *type, float x, float y, float h, int px); /* Movie / TV / YouTube pill on a poster */
#endif
