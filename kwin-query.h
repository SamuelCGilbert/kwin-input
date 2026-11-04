// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
// Capture a logical area at native resolution into a raw BGRA file.
// reply: "width height stride format"
int kq_capture(int x, int y, int w, int h, const char *path, char *reply, size_t reply_len);
// reply: "x y" (logical)
int kq_pointer(char *reply, size_t reply_len);
// reply: "name x y w h scale;..." (logical)
int kq_outputs(char *reply, size_t reply_len);
// Print the user's real input events (needs the AutoKey KWin bridge plugin)
int kq_monitor(void);
