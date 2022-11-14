
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
	off_t								flen;
} ngx_http_footer_loc_conf_t;


typedef struct {
    ngx_str_t                           footer;
	ngx_buf_t							*smart_buf;
	off_t								smart_off;
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

	{ ngx_string("sim_file_len"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_footer_loc_conf_t, flen),
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

    if (lcf->variable == (ngx_http_complex_value_t *) -1
        || r->header_only
        || (r->method & NGX_HTTP_HEAD)
        || r != r->main
        || r->headers_out.status == NGX_HTTP_NO_CONTENT
        || ngx_http_test_content_type(r, &lcf->types) == NULL)
    {
        return ngx_http_next_header_filter(r);
    }

    ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_footer_ctx_t));
    if (ctx == NULL) {
       return NGX_ERROR;
    }

    if (ngx_http_complex_value(r, lcf->variable, &ctx->footer) != NGX_OK) {
        return NGX_ERROR;
    }

    ngx_http_set_ctx(r, ctx, ngx_http_footer_filter_module);

	/* baseline allocate smartbuf per request */
	ctx->smart_buf=ngx_create_temp_buf(r->pool,lcf->flen);
	ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
				   "Created smartbuf of size: %d", 
				   ngx_buf_size(ctx->smart_buf));

	ctx->smart_off=0;

    if (r->headers_out.content_length_n != -1) {
		ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
					   "http footer body filter: conent length: %d", 
					   r->headers_out.content_length_n);
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
    ngx_uint_t             last;
    ngx_chain_t           *cl, *nl;
    ngx_http_footer_ctx_t *ctx;

    ngx_http_footer_loc_conf_t *conf;

    conf = ngx_http_get_module_loc_conf(r, ngx_http_footer_filter_module);

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http footer body filter");

    ctx = ngx_http_get_module_ctx(r, ngx_http_footer_filter_module);
    if (ctx == NULL) {
        return ngx_http_next_body_filter(r, in);
    }

    last = 0;

	/* do actual file size / CACHE_LINE_SIZE copies */
    for (cl = in; cl; cl = cl->next) {
         if (cl->buf->last_buf) {
             last = 1;
             break;
         }
    }

    if (last) {
		/* copy to remaining space within SmartDIMM buf */
		nl=in;
		if (nl == NULL){
			ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
						   "NULL start buffer" );
			return NGX_ERROR;
		}
		int ccs=ngx_buf_size(nl->buf);
		while (ctx->smart_off < conf->flen){
			if ( ccs > (conf->flen - ctx->smart_off) ){
				ccs = (conf->flen - ctx->smart_off);
			}
			ngx_log_debug3(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
						   "copy %d to smartbuf, flen: %d, offset: %d", 
						   ccs, conf->flen, ctx->smart_off );
			*(ctx->smart_buf->start+ctx->smart_off)=*(u_char *)"foo";
			ctx->smart_off+=sizeof("foo");
		}
		ctx->smart_buf->last=ctx->smart_buf->start+ctx->smart_off;
		ngx_log_debug3(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
					   "copy %d to smartbuf, flen: %d, offset: %d", 
					   ccs, conf->flen, ctx->smart_off );
	}

	/*
    buf->pos = ctx->footer.data;
    buf->last = buf->pos + ctx->footer.len;
    buf->start = buf->pos;
    buf->end = buf->last;
    buf->last_buf = 1;
    buf->memory = 1;

    if (ngx_buf_size(cl->buf) == 0) {
        cl->buf = buf;
    } else {
        nl = ngx_alloc_chain_link(r->pool);
        if (nl == NULL) {
            return NGX_ERROR;
        }

        nl->buf = buf;
        nl->next = NULL;
        cl->next = nl;
        cl->buf->last_buf = 0;
    }
	*/

    int rc =  ngx_http_next_body_filter(r, in);
	ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
				   "After ret: %d", 
				   ngx_buf_size(ctx->smart_buf));
	return rc;
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
	conf->flen=NGX_CONF_UNSET_SIZE;
    if (conf == NULL) {
        return NULL;
    }

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

