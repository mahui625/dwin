/*
 * @File:   dwin_trans.c 
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
 
#include "dwin_trans.h" 
#include "dwin_obj.h" 
#include "dwin_page.h" 

/* �������� */ 
#define DWIN_REG_WRITE (0x80)
#define DWIN_REG_READ  (0x81)
#define DWIN_VAR_WRITE (0x82)
#define DWIN_VAR_READ  (0x83)

/* DGUSII��ĻӦ�� */ 
#define DWIN_DGUSII_ACKH (0x4F) 
#define DWIN_DGUSII_ACKL (0x4B) 

enum dwin_watch_state
{
    DWIN_WATCH_STATE_IDLE = 0, 
    DWIN_WATCH_STATE_HEADH, 
    DWIN_WATCH_STATE_HEADL, 
    DWIN_WATCH_STATE_DATE
}; 
typedef enum dwin_watch_state dwin_watch_state_t; 

static struct dwin_watch watch; 

static rt_err_t dwin_watch_putc(uint8_t ch)
{
    rt_err_t ret = RT_EOK; 
    
    if(rt_device_write(watch.serial, (-1), &ch, 1) != 1)
    {
        ret = RT_EFULL; 
    }
    
    return ret;
}

static uint8_t dwin_watch_getc(void)
{
    uint8_t ch; 
    
    while(rt_device_read(watch.serial, (-1), &ch, 1) != 1)
    {
        rt_sem_take(watch.rxsem, RT_WAITING_FOREVER);
    }

    return ch;
}

static rt_err_t dwin_watch_rxcb(rt_device_t dev, rt_size_t size)
{
    return rt_sem_release(watch.rxsem);
}


void dwin_paser(uint8_t *data, uint8_t len)
{
    rt_list_t *list = RT_NULL;
    struct dwin_obj *obj = RT_NULL; 
    struct dwin_page *page = RT_NULL; 
    
    page = dwin_page_current(); 
    
    for(list = page->objs.next; list != &(page->objs); list = list->next)
    {
        if(rt_list_entry(list, struct dwin_obj, list)->value_addr == ((data[4]<<8)+(data[5])) && 
           rt_list_entry(list, struct dwin_obj, list)->value_size == data[6])
        {
            obj = rt_list_entry(list, struct dwin_obj, list);
            break;
        }
    }
    
    if(obj != RT_NULL)
    {
        switch(obj->type)
        {
            case DWIN_WIDGET_TYPE_BUTTON:
                obj->cb(RT_NULL); 
            break; 
        }
    }
}

static void dwin_watch_run(void *p)
{
    uint8_t ch = 0; 
    uint8_t index = 0; 
    dwin_watch_state_t state = DWIN_WATCH_STATE_IDLE; 
    
    while(1)
    {
        ch = dwin_watch_getc(); 
        
        /* Watch listen to the frame header high byte */ 
        if(state == DWIN_WATCH_STATE_IDLE) 
        {
            if(ch == DWIN_USING_HEADH) 
            {
                watch.data[index++] = ch; 
                state = DWIN_WATCH_STATE_HEADH; 
                continue; 
            }
            else
            {
                index = 0; 
                state = DWIN_WATCH_STATE_IDLE; 
                continue;
            }
        }
        
        /* Watch listen to the frame header low byte */ 
        if(state == DWIN_WATCH_STATE_HEADH)
        {
            if(ch == DWIN_USING_HEADL)
            {
                watch.data[index++] = ch; 
                state = DWIN_WATCH_STATE_HEADL;
                continue;
            }
            else
            {
                index = 0;
                state = DWIN_WATCH_STATE_IDLE;
                continue;
            }
        }
        
        if(state == DWIN_WATCH_STATE_HEADL)
        {
            watch.data[index++] = ch;
            state = DWIN_WATCH_STATE_DATE;
            continue;
        }
        
        if(state == DWIN_WATCH_STATE_DATE)
        {
            watch.data[index++] = ch; 
            
            if(index == watch.data[2] + 3)
            {
#ifdef DWIN_USING_DEBUG
                DWIN_DBG("Listen to \033[31m%.3dByte\033[0m data: {", watch.data[2]+3); 

                for(index = 0; index < (watch.data[2]+3); index++) 
                {
                    DWIN_PRINT("\033[32m0x%.2x\033[0m ", watch.data[index]); 
                }
                DWIN_PRINT("\b}.\n");
#endif
                /* Data parse */ 
                // DWIN_DBG("Start parse.\n"); 
                dwin_paser(watch.data, watch.data[2]); 
                
                index = 0;
                state = DWIN_WATCH_STATE_IDLE;
                rt_memset(watch.data, 0x00, sizeof(watch.data));
                
                continue;
            }
        }
    }
}

static rt_err_t dwin_watch_start(void) 
{
    /* Create the watch thread */ 
    watch.thread = rt_thread_create("twatch", dwin_watch_run, RT_NULL, 2048, 6, 10); 
    if(watch.thread == RT_NULL) 
    {
        DWIN_DBG("Dwin auto upload watch thread create failed.\n"); 
        return RT_ENOSYS; 
    }

    /* Start the watch thread */ 
    rt_thread_startup(watch.thread); 
    
    return RT_EOK; 
}

static rt_err_t dwin_watch_stop(void)
{
    rt_err_t ret = RT_EOK;
    
    /* Delete the watch thread */ 
    ret = rt_thread_delete(watch.thread); 
    if(ret != RT_EOK)
    {
        DWIN_DBG("Dwin auto upload watch thread delete failed.\n"); 
        return RT_ENOSYS; 
    }
    
    return RT_EOK;
}

/* The dwin miniDGUS ops */ 
#if (DWIN_USING_TYPE == 0)
rt_err_t dwin_reg_read(rt_uint16_t addr, rt_uint8_t *data, rt_uint8_t len)
{
    uint8_t index = 0;
    rt_err_t ret = RT_EOK; 
    uint8_t rx_data[256] = {0}; 
    
    RT_ASSERT(len  != 0); 
    RT_ASSERT(data != RT_NULL); 

    ret = dwin_watch_stop(); 
    if(ret != RT_EOK)
    {
        DWIN_DBG("Read reg failed error code: %d\n", ret); 
        return ret; 
    }
    
    /* Send 0x80 Cmd to The miniDGUS Dwin */ 
    dwin_watch_putc(DWIN_USING_HEADH); 
    dwin_watch_putc(DWIN_USING_HEADL); 
    dwin_watch_putc(3); 
    dwin_watch_putc(DWIN_REG_READ); 
    dwin_watch_putc(DWIN_GET_BYTEL(addr)); 
    dwin_watch_putc(len); 
    
#ifdef DWIN_USING_DEBUG
    DWIN_DBG("Requests \033[31m%.3dByte\033[0m data: {", 6); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_USING_HEADH); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_USING_HEADL); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", 4); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_REG_READ); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_GET_BYTEL(addr)); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m}.\n", len); 
#endif
    
    /* Blocking waits for data */ 
    while(1)
    {
        rx_data[index++] = dwin_watch_getc();
        
        if(index == len+6) 
        {
#ifdef DWIN_USING_DEBUG
            DWIN_DBG("Read reg \033[31m%.3dByte\033[0m data: {", rx_data[2]+3); 
            
            for(index = 0; index < (len+6); index++) 
            {
                DWIN_PRINT("\033[32m0x%.2x\033[0m ", rx_data[index]); 
            }
            DWIN_PRINT("\b}.\n");
#endif
            
            /* Validate response data */ 
            if((rx_data[0] != DWIN_USING_HEADH)     || 
               (rx_data[1] != DWIN_USING_HEADL)     || 
               (rx_data[2] != (len+3))              || 
               (rx_data[3] != DWIN_REG_READ)        || 
               (rx_data[4] != DWIN_GET_BYTEL(addr)) || 
               (rx_data[5] != len))
            {
                DWIN_DBG("Read reg data Validation failed.\n"); 
                
                ret = dwin_watch_start();
                if(ret != RT_EOK)
                {
                    DWIN_DBG("Failed to start watch after read reg.\n"); 
                    return ret; 
                }
                
                return RT_ERROR; 
            }
            else
            {
                rt_memcpy(data, &rx_data[6], len); 
                break; 
            }
        }
    }
    
    ret = dwin_watch_start();
    if(ret != RT_EOK)
    {
        DWIN_DBG("Failed to start watch after read reg.\n"); 
        return ret; 
    }
    
    return RT_EOK; 
}

rt_err_t dwin_reg_write(rt_uint16_t addr, rt_uint8_t *data, rt_uint8_t len)
{
    uint8_t index = 0; 
    
    RT_ASSERT(len  != 0);  
    RT_ASSERT(data != RT_NULL); 
    
    /* Send 0x80 Cmd to The miniDGUS Dwin */ 
    dwin_watch_putc(DWIN_USING_HEADH); 
    dwin_watch_putc(DWIN_USING_HEADL); 
    dwin_watch_putc(len + 2); 
    dwin_watch_putc(DWIN_REG_WRITE); 
    dwin_watch_putc(DWIN_GET_BYTEL(addr)); 
    
    for(index = 0; index < len; index++)
    {
        dwin_watch_putc(data[index]);
    }
    
#ifdef DWIN_USING_DEBUG
    DWIN_DBG("Wite reg \033[31m%.3dByte\033[0m data: {", 6); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_USING_HEADH); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_USING_HEADL); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", len + 2); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_REG_WRITE); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_GET_BYTEL(addr)); 
    
    for(index = 0; index < len; index++)
    {
        DWIN_PRINT("\033[32m0x%.2x\033[0m ", data[index]); 
    }
    DWIN_PRINT("\b}.\n"); 
#endif
    
    return RT_EOK; 
}

rt_err_t dwin_var_read(rt_uint16_t addr, rt_uint16_t *data, rt_uint8_t len)
{
    uint8_t index = 0;
    rt_err_t ret = RT_EOK; 
    uint8_t rx_data[256] = {0}; 
    
    RT_ASSERT((len >= 1) && (len <= 0x7F)); 
    RT_ASSERT(data != RT_NULL); 
    
    ret = dwin_watch_stop(); 
    if(ret != RT_EOK)
    {
        DWIN_DBG("Read var failed error code: %d\n", ret); 
        return ret; 
    }
    
    /* Send 0x83 Cmd to The DGUS-II Dwin */ 
    dwin_watch_putc(DWIN_USING_HEADH); 
    dwin_watch_putc(DWIN_USING_HEADL); 
    dwin_watch_putc(4); 
    dwin_watch_putc(DWIN_VAR_READ); 
    dwin_watch_putc(DWIN_GET_BYTEH(addr)); 
    dwin_watch_putc(DWIN_GET_BYTEL(addr)); 
    dwin_watch_putc(len); 
    
#ifdef DWIN_USING_DEBUG
    DWIN_DBG("Requests \033[31m%.3dByte\033[0m data: {", 7); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_USING_HEADH); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_USING_HEADL); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", 4); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_VAR_READ); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_GET_BYTEH(addr)); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_GET_BYTEL(addr)); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m}.\n", len); 
#endif
    
    /* Blocking waits for data */ 
    while(1)
    {
        rx_data[index++] = dwin_watch_getc(); 
        
        if(index == (len*2+7))
        {
#ifdef DWIN_USING_DEBUG
            DWIN_DBG("Read var \033[31m%.3dByte\033[0m data: {", rx_data[2]+3); 
            
            for(index = 0; index < (len*2+7); index++) 
            {
                DWIN_PRINT("\033[32m0x%.2x\033[0m ", rx_data[index]); 
            }
            DWIN_PRINT("\b}.\n");
#endif
            
            /* Validate response data */ 
            if((rx_data[0] != DWIN_USING_HEADH)     || 
               (rx_data[1] != DWIN_USING_HEADL)     || 
               (rx_data[2] != (len*2+4))            || 
               (rx_data[3] != DWIN_VAR_READ)        || 
               (rx_data[4] != DWIN_GET_BYTEH(addr)) || 
               (rx_data[5] != DWIN_GET_BYTEL(addr)) || 
               (rx_data[6] != len))
            {
                DWIN_DBG("Read var data Validation failed.\n"); 
                
                ret = dwin_watch_start();
                if(ret != RT_EOK)
                {
                    DWIN_DBG("Failed to start watch after read var.\n"); 
                    return ret; 
                }
                
                return RT_ERROR; 
            }
            else
            {
                for(index = 7; index < (len*2+7); index+=2)
                {
                    data[(index-7)/2] = (rx_data[index]<<8) + rx_data[index + 1];
                }
                break; 
            }
        }
    }
    
    ret = dwin_watch_start();
    if(ret != RT_EOK)
    {
        DWIN_DBG("Failed to start watch after read var.\n"); 
        return ret; 
    }
    
    return RT_EOK; 
}

rt_err_t dwin_var_write(rt_uint16_t addr, rt_uint16_t *data, rt_uint8_t len)
{
    uint8_t index = 0; 
    
    RT_ASSERT(len >= 1); 
    RT_ASSERT(data != RT_NULL); 
    
    /* Send 0x82 Cmd to The DGUS-II Dwin */ 
    dwin_watch_putc(DWIN_USING_HEADH); 
    dwin_watch_putc(DWIN_USING_HEADL); 
    dwin_watch_putc((len*2) + 3); 
    dwin_watch_putc(DWIN_VAR_WRITE); 
    dwin_watch_putc(DWIN_GET_BYTEH(addr)); 
    dwin_watch_putc(DWIN_GET_BYTEL(addr)); 
    
    for(index = 0; index < len; index++)
    {
        dwin_watch_putc(DWIN_GET_BYTEH(data[index]));
        dwin_watch_putc(DWIN_GET_BYTEL(data[index]));
    }
    
#ifdef DWIN_USING_DEBUG
    DWIN_DBG("Wite var \033[31m%.3dByte\033[0m data: {", (len*2)+6); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_USING_HEADH); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_USING_HEADL); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", (len*2) + 3); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_VAR_WRITE); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_GET_BYTEH(addr)); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_GET_BYTEL(addr)); 
    
    for(index = 0; index < len; index++)
    {
        DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_GET_BYTEH(data[index])); 
        DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_GET_BYTEL(data[index])); 
    }
    DWIN_PRINT("\b}.\n"); 
#endif
    
    return RT_EOK; 
}

#endif

/* The dwin DGUSI ops */ 
#if (DWIN_USING_TYPE == 1)
rt_err_t dwin_reg_read (rt_uint16_t addr, rt_uint8_t *data, rt_uint8_t len)
{
    return RT_ERROR; 
}

rt_err_t dwin_reg_write(rt_uint16_t addr, rt_uint8_t *data, rt_uint8_t len)
{
    return RT_ERROR; 
}

rt_err_t dwin_var_read (rt_uint16_t addr, rt_uint16_t *data, rt_uint8_t len)
{
    return RT_ERROR; 
}

rt_err_t dwin_var_write(rt_uint16_t addr, rt_uint16_t *data, rt_uint8_t len)
{
    return RT_ERROR; 
}
#endif

/* The dwin DGUSII ops */ 
#if (DWIN_USING_TYPE == 2)
rt_err_t dwin_reg_read(rt_uint16_t addr, rt_uint8_t *data, rt_uint8_t len)
{
    uint8_t index = 0;
    rt_err_t ret = RT_EOK; 
    uint8_t rx_data[256] = {0}; 
    
    RT_ASSERT(len  != 0); 
    RT_ASSERT(data != RT_NULL); 

    ret = dwin_watch_stop(); 
    if(ret != RT_EOK)
    {
        DWIN_DBG("Read reg failed error code: %d\n", ret); 
        return ret; 
    }
    
    /* Send 0x80 Cmd to The DGUS-II Dwin */ 
    dwin_watch_putc(DWIN_USING_HEADH); 
    dwin_watch_putc(DWIN_USING_HEADL); 
    dwin_watch_putc(4); 
    dwin_watch_putc(DWIN_REG_READ); 
    dwin_watch_putc(DWIN_GET_BYTEH(addr)); 
    dwin_watch_putc(DWIN_GET_BYTEL(addr)); 
    dwin_watch_putc(len); 
    
#ifdef DWIN_USING_DEBUG
    DWIN_DBG("Requests \033[31m%.3dByte\033[0m data: {", 7); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_USING_HEADH); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_USING_HEADL); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", 4); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_REG_READ); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_GET_BYTEH(addr)); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_GET_BYTEL(addr)); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m}.\n", len); 
#endif
    
    /* Blocking waits for data */ 
    while(1)
    {
        rx_data[index++] = dwin_watch_getc();
        
        if(index == len+7) 
        {
#ifdef DWIN_USING_DEBUG
            DWIN_DBG("Read reg \033[31m%.3dByte\033[0m data: {", rx_data[2]+3); 
            
            for(index = 0; index < (len+7); index++) 
            {
                DWIN_PRINT("\033[32m0x%.2x\033[0m ", rx_data[index]); 
            }
            DWIN_PRINT("\b}.\n");
#endif
            
            /* Validate response data */ 
            if((rx_data[0] != DWIN_USING_HEADH)           || 
               (rx_data[1] != DWIN_USING_HEADL)           || 
               (rx_data[2] != (len+4))                    || 
               (rx_data[3] != DWIN_REG_READ)              || 
               (rx_data[4] != DWIN_GET_BYTEH(addr)) || 
               (rx_data[5] != DWIN_GET_BYTEL(addr)) || 
               (rx_data[6] != len))
            {
                DWIN_DBG("Read reg data Validation failed.\n"); 
                
                ret = dwin_watch_start();
                if(ret != RT_EOK)
                {
                    DWIN_DBG("Failed to start watch after read reg.\n"); 
                    return ret; 
                }
                
                return RT_ERROR; 
            }
            else
            {
                rt_memcpy(data, &rx_data[7], len); 
                break; 
            }
        }
    }
    
    ret = dwin_watch_start();
    if(ret != RT_EOK)
    {
        DWIN_DBG("Failed to start watch after read reg.\n"); 
        return ret; 
    }
    
    return RT_EOK; 
}

rt_err_t dwin_reg_write(rt_uint16_t addr, rt_uint8_t *data, rt_uint8_t len)
{
    uint8_t index = 0; 
    rt_err_t ret = RT_EOK; 
    uint8_t rx_data[6] = {0}; 
    
    RT_ASSERT(len  != 0);  
    RT_ASSERT(data != RT_NULL); 
    
    ret = dwin_watch_stop(); 
    if(ret != RT_EOK)
    {
        DWIN_DBG("Wite reg failed error code: %d\n", ret); 
        return ret; 
    }
    
    /* Send 0x80 Cmd to The DGUS-II Dwin */ 
    dwin_watch_putc(DWIN_USING_HEADH); 
    dwin_watch_putc(DWIN_USING_HEADL); 
    dwin_watch_putc(len + 3); 
    dwin_watch_putc(DWIN_REG_WRITE); 
    dwin_watch_putc(DWIN_GET_BYTEH(addr)); 
    dwin_watch_putc(DWIN_GET_BYTEL(addr)); 
    
    for(index = 0; index < len; index++)
    {
        dwin_watch_putc(data[index]);
    }
    
#ifdef DWIN_USING_DEBUG
    DWIN_DBG("Wite reg \033[31m%.3dByte\033[0m data: {", 7); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_USING_HEADH); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_USING_HEADL); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", len + 3); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_REG_WRITE); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_GET_BYTEH(addr)); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_GET_BYTEL(addr)); 
    
    for(index = 0; index < len; index++)
    {
        DWIN_PRINT("\033[32m0x%.2x\033[0m ", data[index]); 
    }
    DWIN_PRINT("\b}.\n"); 
#endif
    
    index = 0; 
    while(1) 
    {
        rx_data[index++] = dwin_watch_getc(); 
        
        if(index == 6) 
        {
#ifdef DWIN_USING_DEBUG
            DWIN_DBG("Response \033[31m%.3dByte\033[0m data: {", rx_data[2]+3); 
            
            for(index = 0; index < (rx_data[2]+3); index++) 
            {
                DWIN_PRINT("\033[32m0x%.2x\033[0m ", rx_data[index]); 
            }
            DWIN_PRINT("\b}.\n");
#endif
            
            /* Validate response data */ 
            if((rx_data[0] != DWIN_USING_HEADH) || 
               (rx_data[1] != DWIN_USING_HEADL) || 
               (rx_data[2] != 3)                || 
               (rx_data[3] != DWIN_REG_WRITE)   || 
               (rx_data[4] != DWIN_DGUSII_ACKH) || 
               (rx_data[5] != DWIN_DGUSII_ACKL))
            {
                DWIN_DBG("Write reg response valid failed.\n"); 
                
                ret = dwin_watch_start();
                if(ret != RT_EOK)
                {
                    DWIN_DBG("Failed to start watch after write reg.\n"); 
                    return ret; 
                }
                
                return RT_ERROR; 
            }
            else
            {
                break; 
            }
        }
    }
    
    ret = dwin_watch_start();
    if(ret != RT_EOK)
    {
        DWIN_DBG("Failed to start watch after write var.\n"); 
        return ret; 
    }
    
    return RT_EOK; 
}

rt_err_t dwin_var_read(rt_uint16_t addr, rt_uint16_t *data, rt_uint8_t len)
{
    uint8_t index = 0;
    rt_err_t ret = RT_EOK; 
    uint8_t rx_data[256] = {0}; 
    
    RT_ASSERT((len >= 1) && (len <= 0x7D)); 
    RT_ASSERT(data != RT_NULL); 
    
    ret = dwin_watch_stop(); 
    if(ret != RT_EOK)
    {
        DWIN_DBG("Read var failed error code: %d\n", ret); 
        return ret; 
    }
    
    /* Send 0x83 Cmd to The DGUS-II Dwin */ 
    dwin_watch_putc(DWIN_USING_HEADH); 
    dwin_watch_putc(DWIN_USING_HEADL); 
    dwin_watch_putc(4); 
    dwin_watch_putc(DWIN_VAR_READ); 
    dwin_watch_putc(DWIN_GET_BYTEH(addr)); 
    dwin_watch_putc(DWIN_GET_BYTEL(addr)); 
    dwin_watch_putc(len); 
    
#ifdef DWIN_USING_DEBUG
    DWIN_DBG("Requests \033[31m%.3dByte\033[0m data: {", 7); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_USING_HEADH); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_USING_HEADL); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", 4); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_VAR_READ); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_GET_BYTEH(addr)); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_GET_BYTEL(addr)); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m}.\n", len); 
#endif
    
    /* Blocking waits for data */ 
    while(1)
    {
        rx_data[index++] = dwin_watch_getc(); 
        
        if(index == (len*2+7))
        {
#ifdef DWIN_USING_DEBUG
            DWIN_DBG("Read var \033[31m%.3dByte\033[0m data: {", rx_data[2]+3); 
            
            for(index = 0; index < (rx_data[2]+3); index++) 
            {
                DWIN_PRINT("\033[32m0x%.2x\033[0m ", rx_data[index]); 
            }
            DWIN_PRINT("\b}.\n");
#endif
            
            /* Validate response data */ 
            if((rx_data[0] != DWIN_USING_HEADH)     || 
               (rx_data[1] != DWIN_USING_HEADL)     || 
               (rx_data[2] != (len*2+4))            || 
               (rx_data[3] != DWIN_VAR_READ)        || 
               (rx_data[4] != DWIN_GET_BYTEH(addr)) || 
               (rx_data[5] != DWIN_GET_BYTEL(addr)) || 
               (rx_data[6] != len))
            {
                DWIN_DBG("Read var data Validation failed.\n"); 
                
                ret = dwin_watch_start();
                if(ret != RT_EOK)
                {
                    DWIN_DBG("Failed to start watch after read var.\n"); 
                    return ret; 
                }
                
                return RT_ERROR; 
            }
            else
            {
                for(index = 7; index < (len*2+7); index+=2)
                {
                    data[(index-7)/2] = (rx_data[index]<<8) + rx_data[index + 1];
                }
                break; 
            }
        }
    }
    
    ret = dwin_watch_start();
    if(ret != RT_EOK)
    {
        DWIN_DBG("Failed to start watch after read var.\n"); 
        return ret; 
    }
    
    return RT_EOK; 
}

rt_err_t dwin_var_write(rt_uint16_t addr, rt_uint16_t *data, rt_uint8_t len)
{
    uint8_t index = 0; 
    rt_err_t ret = RT_EOK; 
    uint8_t rx_data[6] = {0}; 
    
    RT_ASSERT(len >= 1); 
    RT_ASSERT(data != RT_NULL); 
    
    ret = dwin_watch_stop(); 
    if(ret != RT_EOK)
    {
        DWIN_DBG("Wite var failed err code: %d.\n", ret); 
        return ret; 
    }
    
    /* Send 0x82 Cmd to The DGUS-II Dwin */ 
    dwin_watch_putc(DWIN_USING_HEADH); 
    dwin_watch_putc(DWIN_USING_HEADL); 
    dwin_watch_putc((len*2) + 3); 
    dwin_watch_putc(DWIN_VAR_WRITE); 
    dwin_watch_putc(DWIN_GET_BYTEH(addr)); 
    dwin_watch_putc(DWIN_GET_BYTEL(addr)); 
    
    for(index = 0; index < len; index++)
    {
        dwin_watch_putc(DWIN_GET_BYTEH(data[index]));
        dwin_watch_putc(DWIN_GET_BYTEL(data[index]));
    }
    
#ifdef DWIN_USING_DEBUG
    DWIN_DBG("Wite var \033[31m%.3dByte\033[0m data: {", (len*2)+6); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_USING_HEADH); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_USING_HEADL); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", (len*2) + 3); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_VAR_WRITE); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_GET_BYTEH(addr)); 
    DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_GET_BYTEL(addr)); 
    
    for(index = 0; index < len; index++)
    {
        DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_GET_BYTEH(data[index])); 
        DWIN_PRINT("\033[32m0x%.2x\033[0m ", DWIN_GET_BYTEL(data[index])); 
    }
    DWIN_PRINT("\b}.\n"); 
#endif
    
    index = 0; 
    while(1)
    {
        rx_data[index++] = dwin_watch_getc(); 
        
        if(index == 6) 
        {
#ifdef DWIN_USING_DEBUG
            DWIN_DBG("Response \033[31m%.3dByte\033[0m data: {", rx_data[2]+3); 
            
            for(index = 0; index < (rx_data[2]+3); index++) 
            {
                DWIN_PRINT("\033[32m0x%.2x\033[0m ", rx_data[index]); 
            }
            DWIN_PRINT("\b}.\n");
#endif
            
            /* Validate response data */ 
            if((rx_data[0] != DWIN_USING_HEADH) || 
               (rx_data[1] != DWIN_USING_HEADL) || 
               (rx_data[2] != 3)                || 
               (rx_data[3] != DWIN_VAR_WRITE)   || 
               (rx_data[4] != DWIN_DGUSII_ACKH) || 
               (rx_data[5] != DWIN_DGUSII_ACKL))
            {
                DWIN_DBG("Write var response valid failed.\n"); 
                
                ret = dwin_watch_start();
                if(ret != RT_EOK)
                {
                    DWIN_DBG("Failed to start watch after write var or reg.\n"); 
                    return ret; 
                }
                
                return RT_ERROR; 
            }
            else
            {
                break; 
            }
        }
    }
    
    ret = dwin_watch_start();
    if(ret != RT_EOK)
    {
        DWIN_DBG("Failed to start watch after write var or reg.\n"); 
        return ret; 
    }
    
    return RT_EOK; 
}
#endif /* DWIN_USING_TYPE == 2 */ 

rt_err_t dwin_watch_init(dwin_t dwin, const char *name, rt_uint32_t baudrate)
{
    rt_err_t ret = RT_EOK; 
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT; 
    
    RT_ASSERT(dwin != RT_NULL); 
    RT_ASSERT(name != RT_NULL); 
    
    /* Init watch struct */ 
    rt_memset(&watch, 0x00, sizeof(struct dwin_watch)); 
    dwin->watch = &watch; 
    
    /* Find uart device */ 
    dwin->watch->serial = rt_device_find(name); 
    if(dwin->watch->serial == RT_NULL)
    {
        DWIN_START(RT_FALSE, "Starting find %s.", name); 
        ret = RT_ENOSYS; goto failed; 
    }
    else
    {
        DWIN_START(RT_TRUE, "Starting find %s.", name); 
    }
    
    /* Config uart */ 
    config.baud_rate = baudrate; 
    rt_device_control(dwin->watch->serial, RT_DEVICE_CTRL_CONFIG, (void *)&config); 
    
    /* Open device */ 
    ret = rt_device_open(dwin->watch->serial, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX); 
    if(ret != RT_EOK)
    {
        DWIN_START(RT_FALSE, "Starting open %s.", name); 
        ret = RT_ENOSYS; goto failed; 
    }
    else
    {
        DWIN_START(RT_TRUE, "Starting open %s.", name); 
    }
    
    /* Set the serial port callback function */ 
    rt_device_set_rx_indicate(dwin->watch->serial, dwin_watch_rxcb); 
    
    /* Create a serial to receive asynchronous processing semaphore */ 
    dwin->watch->rxsem = rt_sem_create("dwin_rx", 0, RT_IPC_FLAG_FIFO); 
    if(dwin->watch->rxsem == RT_NULL)
    {
        DWIN_START(RT_FALSE, "Starting create sem."); 
        ret = RT_ENOSYS; goto failed; 
    }
    else
    {
        DWIN_START(RT_TRUE, "Starting create sem."); 
    }
    
    /* Start watch */ 
    ret = dwin_watch_start(); 
    if(ret != RT_EOK)
    {
        DWIN_START(RT_FALSE, "Starting startup watch."); 
        goto failed; 
    }
    else
    {
        DWIN_START(RT_TRUE, "Starting startup watch."); 
    }
    
    return RT_EOK; 
    
failed:
    if(dwin->watch->rxsem != RT_NULL)
    {
        rt_sem_delete(dwin->watch->rxsem); 
    }
    
    if(dwin->watch->serial->rx_indicate != RT_NULL)
    {
        rt_device_set_rx_indicate(dwin->watch->serial, RT_NULL); 
    }
    
    if(dwin->watch->serial->ref_count > 0)
    {
        rt_device_close(dwin->watch->serial); 
        dwin->watch->serial = RT_NULL;
    } 
    
    if(dwin->watch != RT_NULL)
    {
        dwin->watch = RT_NULL; 
    }
    
    return ret; 
}
