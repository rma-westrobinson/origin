/* Watch party: a few devices on the same Stremio account (the phone at work, the TV at home) watching the same title
   in step. A small server (server/watchparty, a Cloudflare Worker) relays play, pause, seek and a title change; the
   app polls it over HTTP. A party is a session a device joins and leaves: leaving only stops the syncing, the film
   goes on playing, and a device that is not in a party plays whatever it likes.
   Invites go to a device through its account's inbox: an invite card with Accept and Decline shows over whatever is on
   the screen, a film included. The account is named to the server only by a hash of the Stremio user id (never the
   sign-in key). The server's address is the "partyUrl" setting (or SB_PARTY_URL), default PARTY_DEFAULT_URL. */
#ifndef SB_PARTY_H
#define SB_PARTY_H
/* A placeholder until the Worker is deployed (server/watchparty/README.md): an address with ".example." in it is
   "not set up", and the app then never contacts any server. Set the real one here or in the "partyUrl" setting. */
#define PARTY_DEFAULT_URL "https://origin-watchparty.rogerma0118.workers.dev"
void party_tick(void); /* once a frame (polls, applies the others' commands, notices this device's own) */
void party_draw(void); /* once a frame after the toasts: the invite card or the party menu, over everything */
int party_key(int k, int down); /* 1 when the card or menu took the key (all keys, while one is up) */
int party_click(float x, float y); /* a click or tap on the card or menu: 1 when it took it */
void party_menu_open(void); /* the Watch party menu (the player's button, Settings, the yellow key) */
int party_available(void); /* signed in to Stremio, and the server set up (the invites setting does not matter) */
int party_active(void); /* in a party now */
void party_leave(void); /* leaves it: the film plays on, unsynced */
/* The chat sidebar (the player's, opened from the Watch party menu): the party's messages, and a way to write one. A
   phone types in a field on the system's keyboard; a TV shows a QR code for a typing page on the party's server
   (server/watchparty, GET /chat/<code>). The film plays on and keeps its keys, except Back (closes the sidebar). */
int party_typing(void); /* the phone's chat field has the keyboard (Backspace deletes there, it is not Back) */
int party_text(const char *utf8); /* typed text ("\b" = Backspace): 1 when the chat field took it */
#endif
