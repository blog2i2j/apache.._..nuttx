/****************************************************************************
 * libs/libc/wchar/lib_wcswidth.c
 *
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2005-2014 Rich Felker, et al.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <wchar.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name:  wcswidth
 *
 * Description:
 *    Determine columns needed for a fixed-size wide character string
 *
 * Input Parameters:
 *   wcs - the wide character string need to calculate
 *   n - the length the wide character string
 *
 * Returned Value:
 *   Return the number of columns needed to represent the wide-character
 *   string pointed by "wcs", but at most "n" wide character. If a
 *   nonprintable wide character occurs among these characters, -1 is
 *   returned
 *
 ****************************************************************************/

int wcswidth(FAR const wchar_t *wcs, size_t n)
{
  int l = 0;
  int k = 0;
  for (; n-- && *wcs && (k = wcwidth(*wcs)) >= 0; l += k, wcs++);

  return k < 0 ? k : l;
}
