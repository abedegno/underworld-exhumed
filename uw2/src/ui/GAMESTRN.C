/* target: seg039_3452 */
/* opts: -mm -1 -G -O -Y -d */
/* The game's strings: DATA\STRINGS.PAK, read and Huffman-decoded on demand, plus up to
   two blocks of strings made at run time, and the helpers that print strings to the
   message scroll and fix up item names. Resident segment seg039_3452, in original order.

   String ids: an id is block << 9 | index (ui.h's STR_* bases). get_string(id) looks the
   block up among the made blocks first (make_string adds strings to block 0x7C, a
   conversation's dynamic strings, and 0x7D, the player's name), and otherwise decodes the
   string from the file with read_string; block 0 means the current conversation's or
   cutscene's block (CutsceneOrConversationStringBlock, conv.h), so conversation code can
   use bare indexes. game_sprint(n) prints string n of block 1 (STR_GAME), and
   game_strings_3 prints up to three block 1 strings run together; most of the game's
   messages go out through these two.

   STRINGS.PAK: a word, the number of Huffman tree nodes; the nodes, 4 bytes each (the
   character, a byte this code does not read, then the left and right child; a node whose
   left child is 0xFF is a leaf, and the root is the last node); a word, the number of
   blocks; for each block its number (word) and file offset (long). At a block's offset: a
   word, its string count, then a word per string, the string's offset relative to the end
   of that table. A string is decoded bit by bit, high bit first, 1 going right, until a
   '|' or 0xFF character or 512 characters.

   Data owned: the tree (StringsPak_Address_Indices, far heap), the open file, the bit
   reader's state, str_buff (eight 512-byte slots used in turn, so a string read_string
   returns stays valid only until eight more have been read) and the two made blocks.

   Name: original (init_strings and get_string are in System Shock's GAMESTRN.C, the
   game's strings in both). */

#include <ctype.h>
#include <alloc.h>
#include <io.h>
#include <stdio.h>
#include "conv.h"
#include "file.h"
#include "object.h"
#include "sys.h"
#include "ui.h"

HOST_LAYOUT_BEGIN
struct StringBlock {
    int16 block;
    char far *strings[512];
    int16 count;
};
HOST_LAYOUT_END

/* The string decoder's buffer and the two cached string blocks: far, so a segment each
   (617D:0000 and 627D:0000, segment table entries 72 and 73), defined in this order. Only
   this file uses them. */
/* name: FM Towns names the buffer str_buff (read_string_); its blocks follow str_file
   (StringsPak_FileHandle here) unnamed, so they were static. */
char far str_buff[0x1000];
static struct StringBlock far Strings[2];

int16 OutString = 0;
int16 string_bits_used = 8;
int16 string_blocks = 0;
char SPACE[] = " ";
char aStrings_pak[] = "strings.pak";
char aRb_4[] = "rb";



/* Empties the two made blocks and loads the Huffman tree; a failure is fatal. */
unsigned char far init_strings(void)
{
    int i, j;
    for (i = 0; i < 2; i++) {
        Strings[i].block = -1;
        Strings[i].count = 0;
        for (j = 0; j < 512; j++) Strings[i].strings[j] = 0;
    }
    if ((i = LoadFileStringsPak_seg039_547()) != 0) first_punt(i);
    return 1;
}

void far free_strings(void) { seg039_3452_5E1(); }

/* The string with this id: a pointer into a made block, or a freshly decoded copy in
   str_buff. Strings past a block's end come back empty. */
char far * far get_string(int id)
{
    int found;
    int string;
    register int i;
    register int block;
    block = (uint16)id >> 9;
    string = id & 0x1ff;
    found = -1;
    for (i = 0; i < string_blocks; i++)
        if (Strings[i].block == block) { found = i; break; }
    if (found >= 0) return Strings[found].strings[string];
    if (block == 0) return read_string(CutsceneOrConversationStringBlock, string);
    return read_string(block, string);
}

/* Adds s (the pointer itself, not a copy) as the next string of a made block, taking one
   of the two block slots if the block is new, and returns its id; 0 if both slots hold
   other blocks. Strings are never freed one by one, only by clear_dynamics. */
int far make_string(char far *s, int block)
{
    int index;
    int found = -1;
    int i;
    for (i = 0; i < string_blocks; i++)
        if (Strings[i].block == block) { found = i; break; }
    if (found < 0) {
        if (string_blocks >= 2) return 0;
        found = string_blocks;
        Strings[found].block = block;
        Strings[found].count = 0;
        for (i = 0; i < 512; i++) Strings[found].strings[i] = 0;
        string_blocks++;
    }
    index = Strings[found].count;
    Strings[found].strings[index] = s;
    Strings[found].count++;
    return index | (block << 9);
}

/* Points an existing made string at new text; returns the id, or 0 if its block is not
   one of the made ones. */
/* name: FM Towns replace_string_ occupies the corresponding slot and stores a new
   pointer. */
int far replace_string(char far *s, int id)
{
    int string;
    int block;
    int found;
    int i;
    block = (uint16)id >> 9;
    string = id & 0x1ff;
    found = -1;
    for (i = 0; i < string_blocks; i++)
        if (Strings[i].block == block) { found = i; break; }
    if (found >= 0) { Strings[found].strings[string] = s; return id; }
    return 0;
}

/* Empties a made block (a conversation's strings when it ends), keeping its slot. */
/* name: FM Towns clear_dynamics_ occupies this slot and clears a block's pointers. */
void far clear_dynamics(int block)
{
    int found = -1;
    int i;
    for (i = 0; i < string_blocks; i++)
        if (Strings[i].block == block) { found = i; break; }
    if (found >= 0) {
        Strings[found].block = block;
        Strings[found].count = 0;
        for (i = 0; i < 512; i++) Strings[found].strings[i] = 0;
    }
}

/* Copies an object's name to dst: for a creature with a whoami of 1 to 0xEF its personal
   name (block 7, string whoami + 0x10, if not empty), else the item name of its id from
   block 4 put into shape by fix_name_string. Returns 0 (and a space) when the item has no
   name. */
int far get_name(char far *dst, struct Object far *obj, char article, char plural)
{
    int item = obj->id & ID_ITEM;
    char far *name;
    unsigned char who;
    if (OBJ_MAJOR(obj) == MAJOR_CREATURE) {
        who = obj->whoami;
        if (who > 0 && (unsigned char)who < 0xf0) {
            name = get_string(((unsigned char)who + 0x10) | STR_CONV);
            if (name && *name) { str_copy(dst, name); return 1; }
        }
    }
    name = get_string(item | STR_OBJNAMES);
    if (name == 0 || name[0] == 0) { str_copy(dst, SPACE); return 0; }
    str_copy(dst, fix_name_string(name, article, plural));
    return 1;
}

/* Item names in block 4 have the form "article_singular&plural" (for example
   "a_sword&swords"; ITEMS.H's comments use the singular). Picks the plural after the '&'
   (or, with none, appends an "s"), or cuts the plural off; then drops the article before
   the '_' or, with article set, turns the '_' into a space. Works in place on s, which
   must have room for the extra "s". */
char far * far fix_name_string(char far *s, unsigned char article, char plural)
{
    char far *amp = FindStringDelimiter(s, '&');
    char far *under;
    if (plural) {
        if (amp) s = amp + 1;
        else { under = s + str_len(s); *under = 's'; under[1] = 0; }
    } else if (amp) *amp = 0;
    under = FindStringDelimiter(s, '_');
    if (under) {
        if (!article) s = under + 1;
        else *under = ' ';
    }
    return s;
}

void far game_sprint(int id)
{
    char far *s = get_string(id | STR_GAME);
    scroll_print(s);
}

void far game_strings_3(int first, int second, int third)
{
    char text[256];
    str_copy(text, get_string(first | STR_GAME));
    if (second >= 0) str_cat(text, get_string(second | STR_GAME));
    if (third >= 0) str_cat(text, get_string(third | STR_GAME));
    scroll_print(text);
}

/* Reads the node count and the Huffman tree from strings.pak into far memory and opens
   the file again for the strings themselves. Returns 0, or an error code for first_punt
   (ERR_READ | 2, ERR_LOWMEM | 1). */
int far LoadFileStringsPak_seg039_547(void)
{
    int file;
    if ((file = our_open(aStrings_pak, 1, 0)) == -1) return ERR_READ | 2;
    read(file, &StringsPak_NoOfNodes, 2);
    StringsPak_Address_Indices = farmalloc((unsigned)(StringsPak_NoOfNodes << 2));
    if (!StringsPak_Address_Indices) {
        close(file);
        return ERR_LOWMEM | 1;
    }
    intoFarBuffer_ovr167_5DA(file, StringsPak_Address_Indices, StringsPak_NoOfNodes << 2);
    close(file);
    if ((StringsPak_FileHandle = data_fopen(aStrings_pak, aRb_4)) == 0) return ERR_READ | 2;
    return 0;
}

void far seg039_3452_5E1(void)
{
    fclose(StringsPak_FileHandle);
    farfree(StringsPak_Address_Indices);
}

/* Decodes string `string` of block `block` from the file into the next slot of
   str_buff, seeking through the block table each time (no index is kept). */
char far * far read_string(int block, int string)
{
    unsigned char c;
    char far *result;
    int16 count, item, relative, string_count;   /* read from the file as words */
    int32 address;
    register int index, found;
    result = str_buff + OutString;
    index = 0;
    fseek(StringsPak_FileHandle, (unsigned)(StringsPak_NoOfNodes << 2) + 2, 0);
    fread(&count, 2, 1, StringsPak_FileHandle);
    for (found = 0; found < count; found++) {
        fread(&item, 2, 1, StringsPak_FileHandle);
        if (item == block) break;
        fseek(StringsPak_FileHandle, 4L, 1);
    }
    if (found == count) { result[index] = 0; return result; }
    fread(&address, 4, 1, StringsPak_FileHandle);
    fseek(StringsPak_FileHandle, address, 0);
    fread(&string_count, 2, 1, StringsPak_FileHandle);
    if (string_count <= string) { result[index] = 0; return result; }
    fseek(StringsPak_FileHandle, (unsigned)(string << 1), 1);
    fread(&relative, 2, 1, StringsPak_FileHandle);
    /* a 16-bit sum, as DOS's. On the host, relative from 8000h up was sign-extended and
       (unsigned) kept 32 bits, and fseek's long made of that was wrong either way: where long is
       64 bits (macOS, Linux) a seek about 4 GB forward, and where it is 32 bits (Windows) a seek
       backwards (only a strings file with a block over 32 KB has such a relative) */
    fseek(StringsPak_FileHandle,
        (uint16)(((string_count - (string + 1)) << 1) + relative), 1);
    string_bits_used = 8;
    c = '|';
    do {
        c = seg039_3452_7B2(StringsPak_FileHandle, StringsPak_NoOfNodes - 1);
        result[index] = c;
        index++;
    } while (c != 0xff && c != '|' && index < 512);
    result[index - 1] = 0;
    OutString += 512;
    if (OutString > 0xfff) OutString = 0;
    return result;
}

/* The next bit of the file, high bit of each byte first; non-zero for a 1. */
int far seg039_3452_781(FILE *file)
{
    int bit;
    if (string_bits_used == 8) {
        string_bits = fgetc(file);
        string_bits_used = 0;
    }
    bit = string_bits & 0x80;
    string_bits <<= 1;
    string_bits_used++;
    return bit;
}

/* Walks the Huffman tree from node index (the root) to a leaf, one bit per step, and
   returns the leaf's character. */
int far seg039_3452_7B2(FILE *file, int index)
{
    register int value;
    register FILE *f;
    f = file;
    value = index;
    while (StringsPak_Address_Indices[value].left != 0xff) {
        if (seg039_3452_781(f)) value = StringsPak_Address_Indices[value].right;
        else value = StringsPak_Address_Indices[value].left;
    }
    return StringsPak_Address_Indices[value].value;
}

char far * far seg039_3452_814(char far *s)
{
    char far *start = s;
    while (*s) { if (islower(*s)) *s = *s - 0x20; s++; }
    return start;
}

/* Lower-cases a far string in place and returns it (BABL.C compares strings this way). */
char far * far seg039_3452_857(char far *s)
{
    char far *start = s;
    while (*s) { if (isupper(*s)) *s = *s + 0x20; s++; }
    return start;
}

/* atoi for a far string (BABL.C's conversation built-in that turns a string into a
   number): skips leading white space, then reads decimal digits. The minus sign is
   looked for at s[0] only, so a sign after leading spaces stops the number at 0, and
   "-5" works only because the sign is the first character. */
/* isspace and isdigit by hand: <ctype.h>'s _ctype indexed with the character as a signed
   char, plus one (the C library's table at DS:1BF6 starts with the entry for EOF). */
int far seg039_3452_89A(char far *s)
{
    int i = 0, n = 0;
    while ((_ctype + 1)[(signed char)s[i]] & _IS_SP) i++;
    i += (*s == '-');
    while ((_ctype + 1)[(signed char)s[i]] & _IS_DIG) {
        n *= 10;
        n += (signed char)s[i] - '0';
        i++;
    }
    return *s == '-' ? -n : n;
}

/* strcat for far strings, returning dst. */
char far * far str_cat(char far *dst, char far *src)
{
    char far *start = dst;
    while (*dst) dst++;
    while (*src) { *dst = *src; src++; dst++; }
    *dst = *src; src++; dst++;
    return start;
}
