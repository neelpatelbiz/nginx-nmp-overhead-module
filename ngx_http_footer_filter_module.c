
/*
 * Copyright (C) 2010-2012 Alibaba Group Holding Limited
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    ngx_hash_t                          types;
    ngx_array_t                        *types_keys;
    ngx_http_complex_value_t           *variable;
	size_t								file_len;
} ngx_http_footer_loc_conf_t;


typedef struct {
    ngx_str_t                           footer;
	ngx_buf_t							*smart_buf;
	size_t								smart_off;
	size_t								file_len;
} ngx_http_footer_ctx_t;


static char *ngx_http_footer_filter(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static void *ngx_http_footer_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_footer_merge_loc_conf(ngx_conf_t *cf,
    void *parent, void *child);
static ngx_int_t ngx_http_footer_filter_init(ngx_conf_t *cf);


static ngx_command_t  ngx_http_footer_filter_commands[] = {

    { ngx_string("footer"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_footer_filter,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("footer_types"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_http_types_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_footer_loc_conf_t, types_keys),
      &ngx_http_html_default_types[0] },

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


static ngx_http_output_header_filter_pt ngx_http_next_header_filter;
static ngx_http_output_body_filter_pt   ngx_http_next_body_filter;


static ngx_int_t
ngx_http_footer_header_filter(ngx_http_request_t *r)
{
    ngx_http_footer_ctx_t       *ctx;
    ngx_http_footer_loc_conf_t  *lcf;

    lcf = ngx_http_get_module_loc_conf(r, ngx_http_footer_filter_module);

    ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_footer_ctx_t));
    if (ctx == NULL) {
       return NGX_ERROR;
    }
    if (lcf->variable == (ngx_http_complex_value_t *) -1
        || r->header_only
        || (r->method & NGX_HTTP_HEAD)
        || r != r->main
        || r->headers_out.status == NGX_HTTP_NO_CONTENT
        || ngx_http_test_content_type(r, &lcf->types) == NULL)
    {
        return ngx_http_next_header_filter(r);
    }
	ctx->file_len=lcf->file_len;
	ctx->smart_buf=ngx_create_temp_buf(r->pool, ctx->file_len);
	ctx->smart_off=0;
    ngx_http_set_ctx(r, ctx, ngx_http_footer_filter_module);
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "Created SmartBuf for storing file of len: %d ", 
				   ctx->file_len);


    if (ngx_http_complex_value(r, lcf->variable, &ctx->footer) != NGX_OK) {
        return NGX_ERROR;
    }


    if (r->headers_out.content_length_n != -1) {
        r->headers_out.content_length_n += ctx->footer.len;
    }

    if (r->headers_out.content_length) {
        r->headers_out.content_length->hash = 0;
        r->headers_out.content_length = NULL;
    }

    ngx_http_clear_accept_ranges(r);

    return ngx_http_next_header_filter(r);
}


static ngx_int_t
ngx_http_footer_body_filter(ngx_http_request_t *r, ngx_chain_t *in)
{
    size_t             size;
    ngx_buf_t             *b, *buf;
    ngx_chain_t           *cl;
    ngx_http_footer_ctx_t *ctx;
	//ngx_uint_t 		   last=0;




    ctx = ngx_http_get_module_ctx(r, ngx_http_footer_filter_module);
    if (ctx == NULL) {
        return ngx_http_next_body_filter(r, in);
    }
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http footer body filter");


	buf=ctx->smart_buf;
	cl=in;
	b = cl->buf;
	size = b->last - b->pos;
	//buf = ngx_create_temp_buf(r->pool, size);
	/* do actual file size / CACHE_LINE_SIZE copies */
    if( r->header_only
        || (r->method & NGX_HTTP_HEAD)
        || r != r->main
        || r->headers_out.status == NGX_HTTP_NO_CONTENT)
	{
		ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
					   "hit header only/ no content");
		return  ngx_http_next_body_filter(r, in);
	}
    for (cl = in; cl; cl = cl->next) {
		b = cl->buf;
        size = b->last - b->pos;
		buf->last = ngx_cpymem((buf->pos+ctx->smart_off), b->pos, size);
		ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
					   "comp_cpy: @offset %d size:%d", ctx->smart_off, size);
		ctx->smart_off+=size;
         if (cl->buf->last_buf) {
			 //last = 1;
             break;
         }
    }

	/*
	while ( last && ctx->smart_off < ctx->file_len
			&& b->last > b->pos){
		size=ctx->file_len-ctx->smart_off;
		if ( size > (size_t)ngx_buf_size(b) )
			size=ngx_buf_size(b);
		ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
					   "comp_cpy remaining: @ %d", ctx->smart_off);
		buf->last=ngx_cpymem((buf->pos+ctx->smart_off), b->pos,
				size);
		ctx->smart_off+=size;
	}


	if (last && ctx->smart_off < ctx->file_len)
	{
		while ( ctx->smart_off < ctx->file_len ){
			ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
						   "comp_cpy remaining ++ cl-buf has no content: @ %d", ctx->smart_off);
			size=ctx->file_len-ctx->smart_off;
			if ( size > ctx->smart_off )
				size = ctx->smart_off;
			buf->last=ngx_cpymem((buf->pos+ctx->smart_off), buf->pos,
							size);
			ctx->smart_off+=size;
		}
	}
	*/
	/*
	if ( last && in != NULL ){
		while ( ctx->smart_off < ctx->file_len && size > 0){
			if ( ctx->smart_off <= ctx->file_len - ctx->smart_off )
				size = ctx->smart_off;
			else
				size = ctx->file_len - ctx->smart_off;

			ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
						   "comp_cpy remaining: to offset %d, size: %d", ctx->smart_off, size);
			buf->last=ngx_cpymem((buf->pos+ctx->smart_off), buf->pos,
					size);
			ctx->smart_off += size;
		}
	}
	*/
    return  ngx_http_next_body_filter(r, in);
}


static char *
ngx_http_footer_filter(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_footer_loc_conf_t *flcf = conf;

    ngx_str_t                    *value;
    ngx_http_complex_value_t    **cv;

    cv = &flcf->variable;

    if (*cv != NULL) {
        return "is duplicate";
    }

    value = cf->args->elts;

    if (value[1].len) {
        cmd->offset = offsetof(ngx_http_footer_loc_conf_t, variable);
        return ngx_http_set_complex_value_slot(cf, cmd, conf);
    }

    *cv = (ngx_http_complex_value_t *) -1;

    return NGX_OK;
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

    if (ngx_http_merge_types(cf, &conf->types_keys, &conf->types,
                             &prev->types_keys,&prev->types,
                             ngx_http_html_default_types)
        != NGX_OK)
    {
       return NGX_CONF_ERROR;
    }

    if (conf->variable == NULL) {
        conf->variable = prev->variable;
    }

    if (conf->variable == NULL) {
        conf->variable = (ngx_http_complex_value_t *) -1;
    }


    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_footer_filter_init(ngx_conf_t *cf)
{
    ngx_http_next_body_filter = ngx_http_top_body_filter;
    ngx_http_top_body_filter = ngx_http_footer_body_filter;

    ngx_http_next_header_filter = ngx_http_top_header_filter;
    ngx_http_top_header_filter = ngx_http_footer_header_filter;

    return NGX_OK;
}

