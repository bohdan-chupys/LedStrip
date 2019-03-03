#include "ws2812.h"
#include "recurrent.h"

#include <math.h>

static struct ws2812_list_handler led_buffer = {
    .read = NULL,
    .write = NULL,
};

static struct __led_buffer_node **__alloc_ring_buffer(struct __led_buffer_node **prev)
{
    static int recursion_count = 0;

    *prev = (struct __led_buffer_node *)malloc(sizeof(struct __led_buffer_node));

    if(*prev == NULL)
        return NULL;

    (*prev)->buffer = led_buffer.buffer.dma_buffer[recursion_count];
    (*prev)->col = led_buffer.buffer.col[recursion_count];
    (*prev)->state = LB_STATE_BUSY;

    if(recursion_count != (BUFFER_COUNT - 1)) {
        recursion_count++;
        return __alloc_ring_buffer(&((*prev)->next));
    } else {
        return prev;
    }
}

static void __rgb2dma(union __color *in, struct __dma_buffer *dst)
{
    uint8_t i;

    for(i = 0; i < 8; i++)
    {
        dst->R[7 - i] = ((in->rgb.r) & (1 << i)) ? LED_CODE_ONE : LED_CODE_ZERO;
        dst->G[7 - i] = ((in->rgb.g) & (1 << i)) ? LED_CODE_ONE : LED_CODE_ZERO;
        dst->B[7 - i] = ((in->rgb.b) & (1 << i)) ? LED_CODE_ONE : LED_CODE_ZERO;
    }
}

static void __hsv2rgb(struct __hsv_buffer *src, struct __rgb_buffer *dst)
{
    double c, x, m;
    double s_scale = src->s / 100.0;
    double v_scale = src->v / 100.0;

    c = v_scale * s_scale;
    m = v_scale - c;
    x = c * (1 - fabs(fmod(src->h / 60.0f, 2) - 1));

    switch(src->h/60)
    {
        case 0 :
            dst->r = (uint16_t)((c + m) * 255);
            dst->g = (uint8_t)((x + m) * 255);
            dst->b = (uint8_t)(m * 255);
        break;
        case 1:
            dst->r = (uint16_t)((x + m) * 255);
            dst->g = (uint8_t)((c + m) * 255);
            dst->b = (uint8_t)(m * 255);
        break;
        case 2:
            dst->r = (uint16_t)(m * 255);
            dst->g = (uint8_t)((c + m) * 255);
            dst->b = (uint8_t)((x + m) * 255);
        break;
        case 3:
            dst->r = (uint16_t)(m * 255);
            dst->g = (uint8_t)((x + m) * 255);
            dst->b = (uint8_t)((c + m) * 255);
        break;
        case 4:
            dst->r = (uint16_t)((x + m) * 255);
            dst->g = (uint8_t)(m * 255);
            dst->b = (uint8_t)((c + m) * 255);
        break;
        case 5:
        case 6:
            dst->r = (uint16_t)((c + m) * 255);
            dst->g = (uint8_t)(m * 255);
            dst->b = (uint8_t)((x + m) * 255);
        break;
    }
}

static void __hsv2dma(union __color *in, struct __dma_buffer *dst)
{
    uint8_t i;
    struct __rgb_buffer tmp;

    __hsv2rgb(&(in->hsv), &tmp);
    
    for(i = 0; i < 8; i++)
    {
        dst->R[7 - i] = ((tmp.r) & (1 << i)) ? LED_CODE_ONE : LED_CODE_ZERO;
        dst->G[7 - i] = ((tmp.g) & (1 << i)) ? LED_CODE_ONE : LED_CODE_ZERO;
        dst->B[7 - i] = ((tmp.b) & (1 << i)) ? LED_CODE_ONE : LED_CODE_ZERO;
    }

}

static void __prepare_list_handle(void)
{
    struct __led_buffer_node *tmp = led_buffer.read;
    uint8_t i;

    for(i = 0; i < BUFFER_COUNT; i++)
    {
        if(tmp->buffer == led_buffer.buffer.dma_buffer[0]) led_buffer.write = tmp;
        tmp->state = LB_STATE_BUSY;
        tmp = tmp->next;
    }

    led_buffer.read = led_buffer.write;
    led_buffer.wops.to_dma = __rgb2dma;
}

int initialise_buffer(void (*start_dma)(void *ptr, uint16_t size), void (*stop_dma)())
{
    struct __led_buffer_node **last;

    last = __alloc_ring_buffer(&led_buffer.read);

    if(last == NULL) return ENOMEM;

    (*last)->next = led_buffer.read;

    led_buffer.write = led_buffer.read;

    led_buffer.wops.__start_dma_fnc = start_dma;
    led_buffer.wops.__stop_dma_fnc = stop_dma;

    return 0;
}

static struct update_context *parse_recurrent_param(char *param)
{
    struct update_context *result;
    char *token;
    int code = 0, k = 0, b = 0;
    char func[4];

    if(param == NULL)
    {
        result = (struct update_context *)malloc(sizeof(struct update_context_linear));

        if(result == NULL) goto exit;

        result->k = 0;
        result->b = 0;
        result->x_prev = 0;
        TO_LINEAR_CONTEXT(result)->xmax = 255;
        TO_LINEAR_CONTEXT(result)->is_convergens = 1;
        result->update_fnc = recurent_linear_update;
        return result;
    }

    token = strtok(param, ";");
    code = sscanf(token, "%d*%3s+%d", &k, func, &b);

    if(code != 3) goto error;

    if(!strcmp(func, "lin"))
    {
        result = (struct update_context *)malloc(sizeof(struct update_context_linear));

        if(result == NULL) goto exit;

        result->update_fnc = recurent_linear_update;
        result->b = b;
        result->k = k;
        
        token = strtok(NULL, ";");
        code = sscanf(token, "%d...%d", &k, &b);

        if(code != 2) goto error;

        result->x_prev = k;
        TO_LINEAR_CONTEXT(result)->xmax = b;
        TO_LINEAR_CONTEXT(result)->is_convergens = 1;
    }
    else if( (!strcmp(func, "sin")) || (!strcmp(func, "cos")) )
    {
        result = (struct update_context *)malloc(sizeof(struct update_context_trigonometric));

        if(result == NULL) goto exit;

        result->update_fnc = recurent_sin_update;
        result->b = b;
        result->k = k;
        
        token = strtok(NULL, ";");
        code = sscanf(token, "%d...%d", &k, &b);

        if(code != 2) goto error;

        result->x_prev = k;
        TO_TRIGONOMETRIC_CONTEXT(result)->step = b;
        if(!strcmp(func, "cos"))
        {
            result->x_prev += 90;
            if(result->x_prev >= 360) result->x_prev -= 360; 

        }
    }
    else
        goto error;

    goto exit;

error:
    free(result);
    result = NULL;
exit:
    return result;
}

int ws2812_transfer_recurrent(char *r_exp, char *g_exp, char *b_exp, enum supported_colors scheme, uint8_t count)
{
    uint8_t i, j;
    struct update_context *update_r, *update_b, *update_g;
    struct __led_buffer_node *tmp = led_buffer.read;

    update_r = parse_recurrent_param(r_exp);
    update_g = parse_recurrent_param(g_exp);
    update_b = parse_recurrent_param(b_exp);

    if(update_b == NULL || update_r == NULL || update_g == NULL) return 1;

    __prepare_list_handle();

    switch (scheme)
    {
        case RGB:
            led_buffer.wops.to_dma = __rgb2dma;
            break;
        case HSV:
            led_buffer.wops.to_dma = __hsv2dma;
            break;
        default:
            break;
    }

    memset(tmp->buffer, 0, BUFFER_SIZE * WORDS_PER_LED * 4);
    tmp = tmp->next;

    for(i = 1; i < BUFFER_COUNT; i++)
    {
        for(j = 0; j < BUFFER_SIZE; j++)
        {
            tmp->col[j].abstract.third = update_b->update_fnc(update_b);
            tmp->col[j].abstract.second = update_g->update_fnc(update_g);
            tmp->col[j].abstract.first = update_r->update_fnc(update_r);

            led_buffer.wops.to_dma(&(tmp->col[j]), &(tmp->buffer[j]));
        }
        tmp = tmp->next;
    }

    led_buffer.read->state = LB_STATE_IN_PROGRESS;
    led_buffer.wops.__start_dma_fnc((uint32_t *)(led_buffer.buffer.dma_buffer), 
                    BUFFER_COUNT * BUFFER_SIZE * WORDS_PER_LED);
    while(1)
    {
        if(led_buffer.write != led_buffer.read)
        {
            count -= BUFFER_SIZE;
            if(count <= 0) break;

            assert_param(led_buffer.write->state == LB_STATE_FREE);

            for(i = 0; i < BUFFER_SIZE; i++)
            {
                led_buffer.write->col[i].abstract.third = update_b->update_fnc(update_b);
                led_buffer.write->col[i].abstract.second = update_g->update_fnc(update_g);
                led_buffer.write->col[i].abstract.first = update_r->update_fnc(update_r);

                led_buffer.wops.to_dma(&(led_buffer.write->col[i]), &(led_buffer.write->buffer[i]));
            }

            led_buffer.write->state = LB_STATE_BUSY;
            led_buffer.write = led_buffer.write->next;
        }
    }

    led_buffer.wops.__stop_dma_fnc();

    free(update_r);
    free(update_g);
    free(update_b);

    return 0;
}

void ws2812_interrupt()
{
    assert_param(led_buffer.read->state == LB_STATE_IN_PROGRESS);

    led_buffer.read->state = LB_STATE_FREE;
    led_buffer.read = led_buffer.read->next;
    led_buffer.read->state = LB_STATE_IN_PROGRESS;
}

