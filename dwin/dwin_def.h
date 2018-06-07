/*/
 * @File:   dwin_def.h 
 * @Author: liu2guang 
 * @Date:   2018-04-22 14:52:10 
 * 
 * @LICENSE: MIT
 * https://github.com/liu2guang/dwin/blob/master/LICENSE
 * 
 * Change Logs: 
 * Date           Author       Notes 
 * 2018-04-22     liu2guang    update v2 framework. 
 */ 

#ifndef __DWIN_DEF_H__ 
#define __DWIN_DEF_H__ 

#include "rtthread.h"
#include "rtdevice.h" 

/* Default config */ 
#ifndef DWIN_USING_BAUDRATE
#define DWIN_USING_BAUDRATE 115200
#endif

#ifndef DWIN_USING_HEADH
#define DWIN_USING_HEADH 0x5A
#endif
#ifndef DWIN_USING_HEADL
#define DWIN_USING_HEADL 0xA5
#endif

#define DWIN_USING_NUM_MAX_PER_PAGE 64 /* 64 or 128 */ 

#define DWIN_GET_BYTEH(short)  (rt_uint8_t)(((short) & 0xFF00) >> 8)
#define DWIN_GET_BYTEL(short)  (rt_uint8_t)(((short) & 0x00FF) >> 0)
#define DWIN_SET_SHORT(b1, b2) (rt_uint16_t)(((b1)<<8) | ((b2)&0xff))

/* 调试信息 */ 
#define DWIN_PRINT(fmt, ...)              \
do{                                       \
    rt_kprintf(fmt, ##__VA_ARGS__);       \
}while(0)

#define DWIN_INFO(fmt, ...)               \
do{                                       \
    rt_kprintf("[\033[32mdwin\033[0m] "); \
    rt_kprintf(fmt, ##__VA_ARGS__);       \
}while(0)

#ifndef DWIN_USING_DEBUG
#define DWIN_DBG(fmt, ...) 
#define DWIN_START(bool, fmt, ...) 
#else
#define DWIN_DBG(fmt, ...)                \
do{                                       \
    rt_kprintf("[\033[32mdwin\033[0m] "); \
    rt_kprintf(fmt, ##__VA_ARGS__);       \
}while(0)
#define DWIN_START(bool, fmt, ...)                 \
do{                                                \
    rt_kprintf("[\033[32mdwin\033[0m] ");          \
    rt_kprintf(fmt, ##__VA_ARGS__);                \
    if(bool == RT_FALSE)                           \
        rt_kprintf("\t\t[\033[32mFail\033[0m]\n"); \
    else                                           \
        rt_kprintf("\t\t[\033[32mOK\033[0m]\n");   \
}while(0)
#endif

enum dwin_dir
{
    DWIN_DIR_000 = 0, 
    DWIN_DIR_090, 
    DWIN_DIR_180,
    DWIN_DIR_270
}; 
typedef enum dwin_dir dwin_dir_t; 

enum dwin_obj_type
{
    DWIN_WIDGET_TYPE_BUTTON = 0, 
    DWIN_WIDGET_TYPE_NUMBER, 
    DWIN_WIDGET_TYPE_TEXT, 
    DWIN_WIDGET_TYPE_INPUT, 
    DWIN_WIDGET_TYPE_SCALE, 
    DWIN_WIDGET_TYPE_ICON, 
    DWIN_WIDGET_TYPE_QRCODE, 
    DWIN_WIDGET_TYPE_MAX
}; 
typedef enum dwin_obj_type dwin_obj_type_t; 

struct dwin_rtc
{
    rt_uint16_t year; 
    rt_uint8_t  month; 
    rt_uint8_t  day; 
    
    rt_uint8_t  hour; 
    rt_uint8_t  minute; 
    rt_uint8_t  second; 
    
    rt_uint8_t  week;
}; 
typedef struct dwin_rtc *dwin_rtc_t; 

struct dwin_watch
{
    rt_device_t serial; 
    rt_uint32_t baudrate; 
    rt_sem_t rxsem; 
    rt_thread_t thread; 
    
    uint8_t data[256]; 
}; 
typedef struct dwin_watch *dwin_watch_t; 

struct dwin_obj
{
    rt_list_t list; 
    
    enum dwin_obj_type type; 
    
    rt_uint16_t value_addr; 
    rt_uint16_t value_size; 
    
    rt_uint8_t  active; 
    
    void (*cb)(void *p); 
}; 
typedef struct dwin_obj* dwin_obj_t; 

struct dwin_page
{
    rt_list_t list; 
    
    rt_list_t  objs; 
    rt_uint8_t obj_num; 
    
    rt_uint16_t id; 
}; 
typedef struct dwin_page* dwin_page_t; 
    
struct dwin
{
    rt_list_t   pages;          /* 页面链表 */ 
    
    rt_uint16_t page_num;       /* 页面数量 */ 
    
    struct dwin_page* page_cur; /* 当前页面 */ 
    
    rt_bool_t init; 
    dwin_watch_t watch; 
}; 
typedef struct dwin *dwin_t; 

extern struct dwin dwin; 

#endif
