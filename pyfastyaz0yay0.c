#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdint.h>

typedef uint8_t bool;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

#define true 1
#define false 0

#define MAX_RUN_LENGTH 0xFF + 0x12

#undef min
#undef max
inline int min(int a, int b) {
  return a < b ? a : b;
}
inline int max(int a, int b) {
  return a > b ? a : b;
}

inline u16 Swap16(u16 val) {
  return ((val >> 8) & 0x00FF) |
         ((val << 8) & 0xFF00);
}

inline u32 Swap32(u32 val) {
  return ((val >> 24) & 0x000000FF) |
         ((val >>  8) & 0x0000FF00) |
         ((val <<  8) & 0x00FF0000) |
         ((val << 24) & 0xFF000000);
}

static bool next_byte_match_flag = false;
static int next_byte_match_length = 0;
static int next_byte_match_distance = 0;

void simple_rle_encode(char* new_data, int new_length, char* old_data, int old_length, int* length, int* distance) {
  *length = 0; // The length of the match
  *distance = 0; // The distance backwards to the start of the match
  
  if (new_length == 0) {
    return;
  }
  
  // Try every possible offset in the already compressed data.
  for (int i = 0; i < old_length; i++) {
    int current_old_start = i;
    
    // Figure out how many bytes can be copied at this offset.
    int current_copyable_length = 0;
    for (int j = 0; j < new_length; j++) {
      if (old_data[current_old_start + j] != new_data[j]) {
        break; 
      }
      current_copyable_length++;
    }
    
    if (current_copyable_length > *length) {
      *length = current_copyable_length;
      *distance = old_length - i;
      
      if (*length == new_length) {
        break;
      }
    }
  }
}

void get_match_length_and_distance(char* src, int src_off, int src_size, int search_depth, int* length, int* distance) {
  if (next_byte_match_flag) {
    next_byte_match_flag = false;
    *length = next_byte_match_length;
    *distance = next_byte_match_distance;
    return;
  }
  int old_length = min(src_off, search_depth);
  simple_rle_encode(
    &src[src_off],
    min(src_size-src_off, MAX_RUN_LENGTH),
    &src[src_off-old_length],
    old_length,
    length, distance
  );
  
  if (*length >= 3) {
    // Check if the next byte has a match that would compress better than the current byte.
    int src_off_next = src_off+1;
    old_length = min(src_off_next, search_depth);
    simple_rle_encode(
      &src[src_off_next],
      min(src_size-src_off_next, MAX_RUN_LENGTH),
      &src[src_off_next-old_length],
      old_length,
      &next_byte_match_length, &next_byte_match_distance
    );
    
    if (next_byte_match_length >= *length+2) {
      *length = 1;
      *distance = 0;
      next_byte_match_flag = true;
    }
  }
}

static PyObject* compress_yaz0(PyObject* self, PyObject* args) {
  PyObject* uncomp_bytes;
  int search_depth;
  
  if (!PyArg_ParseTuple(args, "Si", &uncomp_bytes, &search_depth)) {
    return NULL; // Error already raised
  }
  
  u8* src;
  src = (u8*)PyBytes_AsString(uncomp_bytes);
  if (!src) {
    return NULL; // Error already raised
  }
  
  int src_size = (int)PyBytes_Size(uncomp_bytes);
  
  char* dst;
  // It's theoretically possible for the compressed data to be larger than the uncompressed data (though this is unlikely unless the data is just completely random bytes).
  // The max size the compressed data can be is a bit over 12.5% more than the uncompressed data, but we use twice the uncompressed size as the buffer size to be safe and simplify things a bit.
  // Also add 0x10 bytes for the header, and limit the minimum data size to 1.
  int max_possible_comp_size = 0x10 + max(1, src_size*2);
  dst = malloc(max_possible_comp_size);
  if(!dst) {
    return PyErr_NoMemory();
  }
  int src_off = 0;
  int dst_off = 0;
  
  memcpy(dst, "Yaz0", 4);
  dst_off += 4;
  dst[dst_off++] = (src_size & 0xFF000000) >> 24;
  dst[dst_off++] = (src_size & 0x00FF0000) >> 16;
  dst[dst_off++] = (src_size & 0x0000FF00) >> 8;
  dst[dst_off++] = (src_size & 0x000000FF);
  memset(dst+dst_off, 0, 8);
  dst_off += 8;
  
  int buffered_blocks = 0;
  char* mask = &dst[dst_off++];
  *mask = 0;
  while (src_off < src_size) {
    if (buffered_blocks == 8) {
      //　We've got all 8 blocks the current block control can support, so add a new block control.
      mask = &dst[dst_off++];
      *mask = 0;
      buffered_blocks = 0;
    }
    
    int match_length, match_distance;
    match_length = 0;
    get_match_length_and_distance(
      src, src_off, src_size,
      search_depth,
      &match_length, &match_distance
    );
    
    if (match_length < 3) {
      // Match is too small to bother compressing, instead copy the byte directly.
      dst[dst_off++] = src[src_off++];
      
      // Set the uncompressed flag bit for this block.
      *mask |= (1 << (7-buffered_blocks));
    } else {
      src_off += match_length;
      
      if (match_length >= 0x12) {
        dst[dst_off++] = ((match_distance-1) & 0xFF00) >> 8;
        dst[dst_off++] = ((match_distance-1) & 0x00FF);
        
        if (match_length > MAX_RUN_LENGTH) {
          match_length = MAX_RUN_LENGTH;
        }
        dst[dst_off++] = (match_length - 0x12);
      } else {
        dst[dst_off]    = (((match_distance-1) >> 8) & 0x0F); // Bits 0xF00 of the distance
        dst[dst_off++] |= (((match_length  -2) << 4) & 0xF0); // Number of bytes to copy
        dst[dst_off++]  = ( (match_distance-1)       & 0xFF); // Bits 0x0FF of the distance
      }
    }
    
    buffered_blocks++;
  }
  
  if (buffered_blocks == 8) {
    // If we stopped right at the end of a block, we instead write a single zero at the end for some reason.
    // I don't think it's necessary in practice, but we do it for maximum accuracy with the original algorithm.
    // NOTE: I believe this is only accurate for TWW and TP. SMS and LM may not do this.
    // TODO: Add an option to disable this behavior?
    dst[dst_off++] = 0;
  }
  
  int dst_size = dst_off;
  PyObject* dst_bytes = PyBytes_FromStringAndSize(dst, dst_size);
  free(dst);
  return dst_bytes;
}

static PyObject* decompress_yaz0(PyObject* self, PyObject* args) {
  PyObject* comp_bytes;
  
  if (!PyArg_ParseTuple(args, "S", &comp_bytes)) {
    return NULL; // Error already raised
  }
  
  u8* src;
  src = (u8*)PyBytes_AsString(comp_bytes);
  if (!src) {
    return NULL; // Error already raised
  }
  
  int src_size = (int)PyBytes_Size(comp_bytes);
  
  if (src_size < 0x10) {
    PyErr_SetString(PyExc_ValueError, "Compressed data is too small, must be at least 16 bytes.");
    return NULL;
  }
  
  int src_off = 4;
  int uncomp_size = 0;
  uncomp_size |= (src[src_off++] << 24) & 0xFF000000;
  uncomp_size |= (src[src_off++] << 16) & 0x00FF0000;
  uncomp_size |= (src[src_off++] << 8 ) & 0x0000FF00;
  uncomp_size |= (src[src_off++]      ) & 0x000000FF;
  
  char* dst;
  dst = malloc(uncomp_size);
  if(!dst) {
    return PyErr_NoMemory();
  }
  int dst_off = 0;
  
  src_off = 0x10;
  
  int mask_bits_left = 0;
  u8 mask = 0;
  while (dst_off < uncomp_size) {
    if (src_off >= src_size) {
      free(dst);
      PyErr_SetString(PyExc_ValueError, "Compressed data is corrupt.");
      return NULL;
    }
    
    if (mask_bits_left == 0) {
      mask = src[src_off++];
      mask_bits_left = 8;
    }
    
    if (mask & 0x80) {
      dst[dst_off++] = src[src_off++];
    } else {
      u8 byte1 = src[src_off++];
      u8 byte2 = src[src_off++];
      
      int distance = ((byte1 & 0x0F) << 8) | byte2;
      int copy_src_offset = dst_off - (distance + 1);
      int length = (byte1 >> 4);
      if (length == 0) {
        length = src[src_off++] + 0x12;
      } else {
        length += 2;
      }
      
      for (int i = 0; i < length; i++) {
        dst[dst_off++] = dst[copy_src_offset++];
      }
    }
    
    mask <<= 1;
    mask_bits_left--;
  }
  
  int dst_size = dst_off;
  PyObject* dst_bytes = PyBytes_FromStringAndSize(dst, dst_size);
  free(dst);
  return dst_bytes;
}

static PyObject* compress_yay0(PyObject* self, PyObject* args) {
  PyObject* uncomp_bytes;
  int search_depth;
  
  if (!PyArg_ParseTuple(args, "Si", &uncomp_bytes, &search_depth)) {
    return NULL; // Error already raised
  }
  
  u8* src;
  src = (u8*)PyBytes_AsString(uncomp_bytes);
  if (!src) {
    return NULL; // Error already raised
  }
  
  u32 src_size = (u32)PyBytes_Size(uncomp_bytes);
  
  char* dst;
  char* mask_buffer;
  char* link_buffer;
  char* chunk_buffer;
  u32 mask_buffer_max_size = (src_size + 32) >> 3;
  u32 link_buffer_max_size = src_size;
  u32 chunk_buffer_max_size = src_size;
  u32 dst_max_size = 0x10 + mask_buffer_max_size + link_buffer_max_size + chunk_buffer_max_size;
  dst = malloc(dst_max_size);
  if(!dst) { return PyErr_NoMemory(); }
  mask_buffer = malloc(mask_buffer_max_size);
  if(!mask_buffer) { return PyErr_NoMemory(); }
  link_buffer = malloc(link_buffer_max_size);
  if(!link_buffer) { return PyErr_NoMemory(); }
  chunk_buffer = malloc(chunk_buffer_max_size);
  if(!chunk_buffer) { return PyErr_NoMemory(); }
  
  u32 mask_offset = 0;
  u32 link_offset = 0;
  u32 chunk_offset = 0;
  
  u32 src_off = 0;
  
  u32 mask_bits_done = 0;
  u32 mask = 0;
  while (src_off < src_size) {
    if (mask_bits_done == 32) {
      // Filled up the mask, so write this block.
      *(u32*)&mask_buffer[mask_offset] = Swap32(mask);
      mask_offset += 4;
      mask = 0;
      mask_bits_done = 0;
    }
    
    u32 match_length, match_distance;
    match_length = 0;
    get_match_length_and_distance(
      src, src_off, src_size,
      search_depth,
      &match_length, &match_distance
    );
    
    if (match_length < 3) {
      // Match is too small to bother compressing, instead copy the byte directly.
      chunk_buffer[chunk_offset++] = src[src_off++];
      
      // Set the uncompressed flag bit for this block.
      mask |= (1 << (31-mask_bits_done));
    } else {
      u16 dist = match_distance - 1;
      u16 link = (dist & 0x0FFF);
      
      if (match_length >= 0x12) {
        if (match_length > MAX_RUN_LENGTH) {
          match_length = MAX_RUN_LENGTH;
        }
        chunk_buffer[chunk_offset++] = (match_length - 0x12);
      } else {
        link |= (match_length - 2) << 12;
      }
      
      *(u16*)&link_buffer[link_offset] = Swap16(link);
      link_offset += 2;
      
      src_off += match_length;
    }
    
    mask_bits_done++;
  }
  
  if (mask_bits_done > 0) {
    // Still some mask bits left over that weren't written yet, so write them now.
    *(u32*)&mask_buffer[mask_offset] = Swap32(mask);
    mask_offset += 4;
  }
  
  memcpy(&dst[0x0], "Yay0", 4);
  *(u32*)&dst[0x4] = Swap32(src_size);
  
  u32 mask_location = 0x10;
  u32 mask_size = mask_offset;
  memcpy(&dst[mask_location], mask_buffer, mask_size);
  
  u32 link_location = mask_location + mask_size;
  u32 link_size = link_offset;
  memcpy(&dst[link_location], link_buffer, link_size);
  *(u32*)&dst[0x8] = Swap32(link_location);
  
  u32 chunk_location = link_location + link_size;
  u32 chunk_size = chunk_offset;
  memcpy(&dst[chunk_location], chunk_buffer, chunk_size);
  *(u32*)&dst[0xC] = Swap32(chunk_location);
  
  u32 dst_size = chunk_location + chunk_size;
  PyObject* dst_bytes = PyBytes_FromStringAndSize(dst, dst_size);
  free(dst);
  free(mask_buffer);
  free(link_buffer);
  free(chunk_buffer);
  return dst_bytes;
}

static PyObject* decompress_yay0(PyObject* self, PyObject* args) {
  PyObject* comp_bytes;
  
  if (!PyArg_ParseTuple(args, "S", &comp_bytes)) {
    return NULL; // Error already raised
  }
  
  u8* src;
  src = (u8*)PyBytes_AsString(comp_bytes);
  if (!src) {
    return NULL; // Error already raised
  }
  
  u32 src_size = (u32)PyBytes_Size(comp_bytes);
  
  if (src_size < 0x10) {
    PyErr_SetString(PyExc_ValueError, "Compressed data is too small, must be at least 16 bytes.");
    return NULL;
  }
  
  u32 uncomp_size = Swap32(*(u32*)&src[4]);
  u32 link_offset = Swap32(*(u32*)&src[8]);
  u32 chunk_offset = Swap32(*(u32*)&src[0xC]);
  u32 mask_offset = 0x10;
  
  char* dst;
  dst = malloc(uncomp_size);
  if(!dst) {
    return PyErr_NoMemory();
  }
  u32 dst_off = 0;
  
  u32 mask_bits_left = 0;
  u32 mask = 0;
  while (dst_off < uncomp_size) {
    if (mask_offset >= src_size) {
      free(dst);
      PyErr_SetString(PyExc_ValueError, "Compressed data is corrupt.");
      return NULL;
    }
    
    if (mask_bits_left == 0) {
      mask = Swap32(*(u32*)&src[mask_offset]);
      mask_offset += 4;
      mask_bits_left = 32;
    }
    
    if (mask & 0x80000000) {
      dst[dst_off++] = src[chunk_offset++];
    } else {
      u16 link = Swap16(*(u16*)&src[link_offset]);
      link_offset += 2;
      
      u16 distance = link & 0x0FFF;
      u32 copy_src_offset = dst_off - (distance + 1);
      u32 length = (link >> 12);
      
      if (length == 0) {
        length = src[chunk_offset++] + 0x12;
      } else {
        length += 2;
      }
      
      for (u32 i = 0; i < length; i++) {
        dst[dst_off++] = dst[copy_src_offset++];
      }
    }
    
    mask <<= 1;
    mask_bits_left--;
  }
  
  int dst_size = dst_off;
  PyObject* dst_bytes = PyBytes_FromStringAndSize(dst, dst_size);
  free(dst);
  return dst_bytes;
}

static PyMethodDef pyfastyaz0yay0Methods[] = {
  {"compress_yaz0", compress_yaz0, METH_VARARGS, "Takes data as a bytes object and returns the data Yaz0 compressed as another bytes object."},
  {"decompress_yaz0", decompress_yaz0, METH_VARARGS, "Takes Yaz0 compressed data as a bytes object and returns the data decompressed as another bytes object."},
  {"compress_yay0", compress_yay0, METH_VARARGS, "Takes data as a bytes object and returns the data Yay0 compressed as another bytes object."},
  {"decompress_yay0", decompress_yay0, METH_VARARGS, "Takes Yay0 compressed data as a bytes object and returns the data decompressed as another bytes object."},
  {NULL, NULL, 0, NULL} // Sentinel
};

static struct PyModuleDef pyfastyaz0yay0_module = {
  PyModuleDef_HEAD_INIT,
  "pyfastyaz0yay0", // Module name
  NULL, // Documentation
  -1, // Size of per-interpreter state of the module, or -1 if the module keeps state in global variables.
  pyfastyaz0yay0Methods
};

PyMODINIT_FUNC PyInit_pyfastyaz0yay0(void) {
  PyObject* module;
  
  module = PyModule_Create(&pyfastyaz0yay0_module);
  if (module == NULL) {
    return NULL;
  }
  
  return module;
}
