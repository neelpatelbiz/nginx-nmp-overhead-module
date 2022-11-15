
/*
 * Copyright (C) 2010-2012 Alibaba Group Holding Limited
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
	size_t								file_len;
} ngx_http_footer_loc_conf_t;


typedef struct {
	ngx_buf_t							*smart_buf;
	size_t								smart_off;
	size_t								file_len;
} ngx_http_footer_ctx_t;


static void *ngx_http_footer_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_footer_merge_loc_conf(ngx_conf_t *cf,
    void *parent, void *child);
static ngx_int_t ngx_http_footer_filter_init(ngx_conf_t *cf);


static ngx_command_t  ngx_http_footer_filter_commands[] = {

    { ngx_string("file_length"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_footer_loc_conf_t, file_len),
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_footer_filter_module_ctx = {
    NULL,                               /* proconfiguration */
    ngx_http_footer_filter_init,        /* postconfiguration */

    NULL,                               /* create main configuration */
    NULL,                               /* init main configuration */

    NULL,                               /* create server configuration */
    NULL,                               /* merge server configuration */

    ngx_http_footer_create_loc_conf,    /* create location configuration */
    ngx_http_footer_merge_loc_conf      /* merge location configuration */
};


ngx_module_t  ngx_http_footer_filter_module = {
    NGX_MODULE_V1,
    &ngx_http_footer_filter_module_ctx, /* module context */
    ngx_http_footer_filter_commands,    /* module directives */
    NGX_HTTP_MODULE,                    /* module type */
    NULL,                               /* init master */
    NULL,                               /* init module */
    NULL,                               /* init process */
    NULL,                               /* init thread */
    NULL,                               /* exit thread */
    NULL,                               /* exit process */
    NULL,                               /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_http_output_body_filter_pt   ngx_http_next_body_filter;

static ngx_int_t
ngx_http_footer_body_filter(ngx_http_request_t *r, ngx_chain_t *in)
{
    size_t             size;
    ngx_buf_t             *buf;
    ngx_chain_t           *cl;
    ngx_http_footer_ctx_t *ctx;

    ctx = ngx_http_get_module_ctx(r, ngx_http_footer_filter_module);
    if (ctx == NULL) {
		ngx_http_footer_loc_conf_t  *lcf;
		lcf = ngx_http_get_module_loc_conf(r, ngx_http_footer_filter_module);
		ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
					   "footer ctx null...allocating");
		/*set the ctx*/
		ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_footer_ctx_t));	

		ngx_http_set_ctx(r, ctx, ngx_http_footer_filter_module);
		ctx->file_len=lcf->file_len;
		ctx->smart_off=0;
		ctx->smart_buf=ngx_palloc(r->pool, ctx->file_len);

    }
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http footer body filter");

	buf=ctx->smart_buf;
	for(cl=in; cl; cl=cl->next)
	{
		ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
					   "comp_cpy");

		ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
					   "macro size:%d, man_size: %d", ngx_buf_size(cl->buf), cl->buf->last - cl->buf->pos);

		ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
					   "setting size to copy to dbuf");
        size = ngx_buf_size(cl->buf);
		ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
					   "copying to smart_buf");
		/* repeat copy file data to start of smartDIMM buffer */
		//buf->last = ngx_cpymem(buf->pos, cl->buf->file_pos, size);
		buf->last = ngx_cpymem(buf->pos, cl->buf->pos, size);
		ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
					   "copied offset: @ %d", ctx->smart_off);
		//ctx->smart_off+=size;
		ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
					   "Incremented smart_off");
    }
	ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
				   "smart_off:%zu full_file:%zu", ctx->smart_off, ctx->file_len );
	

    return  ngx_http_next_body_filter(r, in);
}

static void *
ngx_http_footer_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_footer_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_footer_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }
    conf->file_len = NGX_CONF_UNSET_SIZE;
    /*
     * set by ngx_pcalloc():
     *
     *     conf->types = { NULL };
     *     conf->types_keys = NULL;
     *     conf->variable = NULL;
     */

    return conf;
}


static char *
ngx_http_footer_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_footer_loc_conf_t  *prev = parent;
    ngx_http_footer_loc_conf_t  *conf = child;
	
    ngx_conf_merge_uint_value(conf->file_len, prev->file_len, 0);

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_footer_filter_init(ngx_conf_t *cf)
{
    ngx_http_next_body_filter = ngx_http_top_body_filter;
    ngx_http_top_body_filter = ngx_http_footer_body_filter;

    return NGX_OK;
}

