#include "recurrent.h"

uint8_t recurent_linear_update(struct update_context *ctx)
{
    int16_t result;

    if(ctx->is_convergens)
    {
        result = ctx->k * ctx->x_prev + ctx->b;
        ctx->x_prev++;
        if(result >= ctx->xmax) {
            ctx->is_convergens = 0;
            return ctx->xmax;
        } 
        return (uint8_t)result;
    }
    else
    {
        result = (-1) * ctx->x_prev * ctx->k - ctx->b + (ctx->xmax << 1);
        ctx->x_prev++;
        if(result <= 1) {
            ctx->is_convergens = 1;
            ctx->x_prev = (-1) * (ctx->b / ctx->k);
            return 1;
        }
        return (uint8_t)result;
    }
    
}