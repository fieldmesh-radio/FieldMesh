#pragma once

// FieldMesh: local block list for the companion.
//
// Adverts from a blocked node are dropped before contact handling, so the node is never
// (re-)added as a contact, never shows up as discovered and never reaches the app or the
// map. Only this device ignores the node - relaying in Off-Grid is not affected.
//
// Operated from the normal MeshCore app, the commands never go on air:
//   direct message "!block" to a contact       -> block that contact
//   login with password "!block" (repeater,    -> block that node
//     room server, sensor)
//   channel message "!blocklist"                -> list the blocked nodes
//   channel message "!unblock <n>"              -> remove entry n of the list
//   channel message "!unblock all"              -> clear the list
//
// Header-only without dependencies so the native unit tests can use it.

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FM_BLOCK_MAX        8    // entries in the list
#define FM_BLOCK_KEY_LEN    8    // public key prefix compared (64 bit)
#define FM_BLOCK_NAME_SIZE  16   // stored node name incl. terminating zero (display only)

struct FmBlockList {
  uint8_t keys[FM_BLOCK_MAX][FM_BLOCK_KEY_LEN] = {};
  char names[FM_BLOCK_MAX][FM_BLOCK_NAME_SIZE] = {};

  // A slot is in use when its key is not all zero. Used slots are always at the front.
  bool used(int idx) const {
    if (idx < 0 || idx >= FM_BLOCK_MAX) return false;
    for (int i = 0; i < FM_BLOCK_KEY_LEN; i++) {
      if (keys[idx][i] != 0) return true;
    }
    return false;
  }

  int count() const {
    int n = 0;
    while (n < FM_BLOCK_MAX && used(n)) n++;
    return n;
  }

  // Index of the entry for this public key, or -1.
  int find(const uint8_t* pub_key) const {
    if (pub_key == NULL) return -1;
    for (int i = 0; i < FM_BLOCK_MAX && used(i); i++) {
      if (memcmp(keys[i], pub_key, FM_BLOCK_KEY_LEN) == 0) return i;
    }
    return -1;
  }

  bool contains(const uint8_t* pub_key) const { return find(pub_key) >= 0; }

  // Adds the node (or refreshes the name of an existing entry).
  // Returns the entry index, or -1 when the list is full or the key is invalid.
  int add(const uint8_t* pub_key, const char* name) {
    if (pub_key == NULL) return -1;
    bool all_zero = true;
    for (int i = 0; i < FM_BLOCK_KEY_LEN; i++) {
      if (pub_key[i] != 0) { all_zero = false; break; }
    }
    if (all_zero) return -1;   // would read as an empty slot

    int idx = find(pub_key);
    if (idx < 0) {
      idx = count();
      if (idx >= FM_BLOCK_MAX) return -1;
      memcpy(keys[idx], pub_key, FM_BLOCK_KEY_LEN);
    }
    setName(idx, name);
    return idx;
  }

  // Removes entry idx and moves the following entries up. Returns false if idx is unused.
  bool remove(int idx) {
    if (!used(idx)) return false;
    for (int i = idx; i < FM_BLOCK_MAX - 1; i++) {
      memcpy(keys[i], keys[i + 1], FM_BLOCK_KEY_LEN);
      memcpy(names[i], names[i + 1], FM_BLOCK_NAME_SIZE);
    }
    memset(keys[FM_BLOCK_MAX - 1], 0, FM_BLOCK_KEY_LEN);
    memset(names[FM_BLOCK_MAX - 1], 0, FM_BLOCK_NAME_SIZE);
    return true;
  }

  void clear() {
    memset(keys, 0, sizeof(keys));
    memset(names, 0, sizeof(names));
  }

private:
  // Copies the name, shortened on a UTF-8 character boundary.
  void setName(int idx, const char* name) {
    char* dst = names[idx];
    memset(dst, 0, FM_BLOCK_NAME_SIZE);
    if (name == NULL) return;
    size_t len = strlen(name);
    if (len > FM_BLOCK_NAME_SIZE - 1) {
      len = FM_BLOCK_NAME_SIZE - 1;
      while (len > 0 && (((uint8_t)name[len]) & 0xC0) == 0x80) len--;   // don't split a character
    }
    memcpy(dst, name, len);
  }
};

// ── Commands ─────────────────────────────────────────────────────────────────

enum FmBlockCmd {
  FM_BLOCK_CMD_NONE = 0,
  FM_BLOCK_CMD_BLOCK,         // "!block"
  FM_BLOCK_CMD_LIST,          // "!blocklist"
  FM_BLOCK_CMD_UNBLOCK_ALL,   // "!unblock all"
  FM_BLOCK_CMD_UNBLOCK_ONE,   // "!unblock <n>", n = 1..FM_BLOCK_MAX
  FM_BLOCK_CMD_INVALID,       // "!unblock" with a missing or bad argument
};

// Compares a word case-insensitively; returns the position after it or NULL.
static inline const char* fmBlockMatchWord(const char* p, const char* word) {
  while (*word) {
    if (tolower((unsigned char)*p) != *word) return NULL;
    p++;
    word++;
  }
  return p;
}

static inline const char* fmBlockSkipSpace(const char* p) {
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
  return p;
}

// Parses a message text. Leading/trailing blanks and letter case are ignored, nothing else
// may follow a command (so "!block this guy" stays a normal message).
// *entry receives n-1 for FM_BLOCK_CMD_UNBLOCK_ONE.
static inline FmBlockCmd fmBlockParse(const char* text, int* entry) {
  if (text == NULL) return FM_BLOCK_CMD_NONE;
  const char* p = fmBlockSkipSpace(text);
  const char* q;

  if ((q = fmBlockMatchWord(p, "!blocklist")) != NULL) {
    return *fmBlockSkipSpace(q) == 0 ? FM_BLOCK_CMD_LIST : FM_BLOCK_CMD_NONE;
  }
  if ((q = fmBlockMatchWord(p, "!block")) != NULL) {
    return *fmBlockSkipSpace(q) == 0 ? FM_BLOCK_CMD_BLOCK : FM_BLOCK_CMD_NONE;
  }
  if ((q = fmBlockMatchWord(p, "!unblock")) != NULL) {
    if (*q != 0 && *q != ' ' && *q != '\t') return FM_BLOCK_CMD_NONE;   // e.g. "!unblocked"
    q = fmBlockSkipSpace(q);
    const char* r;
    if ((r = fmBlockMatchWord(q, "all")) != NULL && *fmBlockSkipSpace(r) == 0) {
      return FM_BLOCK_CMD_UNBLOCK_ALL;
    }
    int n = 0, digits = 0;
    while (*q >= '0' && *q <= '9' && digits < 3) {
      n = n * 10 + (*q - '0');
      q++;
      digits++;
    }
    if (digits > 0 && *fmBlockSkipSpace(q) == 0 && n >= 1 && n <= FM_BLOCK_MAX) {
      if (entry) *entry = n - 1;
      return FM_BLOCK_CMD_UNBLOCK_ONE;
    }
    return FM_BLOCK_CMD_INVALID;
  }
  return FM_BLOCK_CMD_NONE;
}

// Text for "!blocklist", e.g. "Blocked 2/8: 1 NodeA, 2 NodeB" or "Blocked 0/8: none".
// Names are shortened to 12 bytes (UTF-8 safe) so a full list fits into one message.
static inline void fmBlockListText(const FmBlockList& list, char* out, size_t out_size) {
  if (out == NULL || out_size == 0) return;
  int n = list.count();
  int len = snprintf(out, out_size, "Blocked %d/%d:", n, FM_BLOCK_MAX);
  if (n == 0 && len >= 0 && (size_t)len < out_size) {
    snprintf(out + len, out_size - len, " none");
    return;
  }
  for (int i = 0; i < n && len >= 0 && (size_t)len < out_size; i++) {
    char name[13];
    size_t nl = strlen(list.names[i]);
    if (nl > sizeof(name) - 1) {
      nl = sizeof(name) - 1;
      while (nl > 0 && (((uint8_t)list.names[i][nl]) & 0xC0) == 0x80) nl--;
    }
    memcpy(name, list.names[i], nl);
    name[nl] = 0;
    if (nl == 0) {   // no name stored: show the key prefix
      snprintf(name, sizeof(name), "%02x%02x%02x%02x",
               list.keys[i][0], list.keys[i][1], list.keys[i][2], list.keys[i][3]);
    }
    len += snprintf(out + len, out_size - len, "%s %d %s", i == 0 ? "" : ",", i + 1, name);
  }
}
