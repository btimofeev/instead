#include "externals.h"
#include "internals.h"
char *err_msg = NULL;

#define ERR_MSG_MAX 512
char	game_cwd[PATH_MAX];
char	*curgame_dir = NULL;

int game_own_theme = 0;

static void game_cursor(int on);
static void mouse_reset(void);
static void menu_toggle(void);

void game_err_msg(const char *s)
{
	if (err_msg)
		free(err_msg);
	if (s) {
		err_msg = strdup(s);
		if (err_msg && strlen(err_msg) > ERR_MSG_MAX) {
			err_msg[ERR_MSG_MAX - 4] = 0;
			strcat(err_msg, "...");
		}
	} else
		err_msg = NULL;
}


static int is_game(const char *path, const char *n)
{
	int rc = 0;
	char *p = getpath(path, n);
	char *pp;
	if (!p)
		return 0;
	pp = malloc(strlen(p) + strlen(MAIN_FILE) + 1);
	if (pp) {
		strcpy(pp, p);
		strcat(pp, MAIN_FILE);
		if (!access(pp, R_OK))
			rc = 1;
		free(pp);
	}
	free(p);
	return rc;
}

struct	game *games = NULL;
int	games_nr = 0;

void free_last(void);
	
int game_select(const char *name)
{
	int i;
	free_last();
	if (!name || !*name) {
		if (games_nr == 1) 
			name = games[0].dir;
		else
			return 0;
	} 
	chdir(game_cwd);
	for (i = 0; i<games_nr; i ++) {
		if (!strcmp(games[i].dir, name)) {
			instead_done();
			if (instead_init())
				return -1;
			if (chdir(games[i].path))
				return -1;
			if (instead_load(MAIN_FILE))
				return -1;
			curgame_dir = games[i].dir;
			return 0;
		}
	}
	return 0;
}


static char *game_name(const char *path, const char *d_name)
{
	int brk = 0;
	char *p = getfilepath(path, MAIN_FILE);
	if (p) {
		char *l; char line[1024];
		FILE *fd = fopen(p, "r");
		free(p);
		if (!fd)
			goto err;

		while ((l = fgets(line, sizeof(line), fd)) && !brk) {
			l = parse_tag(l, "$Name:", "--", &brk);
			if (l)
				return l;
		}
		fclose(fd);
	}
err:
	return strdup(d_name);
}

int games_lookup(const char *path)
{
	char *p;
	int n = 0, i = 0;
	DIR *d;
	struct dirent *de;

	if (!path)
		return 0;

	d = opendir(path);
	if (!d)
		return -1;
	while ((de = readdir(d))) {
		/*if (de->d_type != DT_DIR)
			continue;*/
		if (!is_game(path, de->d_name))
			continue;
		n ++;
	}
		
	rewinddir(d);
	if (!n)
		return 0;
	games = realloc(games, sizeof(struct game) * (n + games_nr));
	while ((de = readdir(d)) && i < n) {
		/*if (de->d_type != DT_DIR)
			continue;*/
		if (!is_game(path, de->d_name))
			continue;
		p = getpath(path, de->d_name);
		games[games_nr].path = p;
		games[games_nr].dir = strdup(de->d_name);
		games[games_nr].name = game_name(p, de->d_name);
		games_nr ++;
		i ++;
	}
	closedir(d);
	return 0;
}


static int motion_mode = 0;
static int motion_id = 0;
static int motion_y = 0;

static		char *last_pict = NULL;
static 		char *last_title = NULL;
static		char *last_music = NULL;
static int mx, my;
static img_t 	menubg = NULL;
static img_t	menu = NULL;

static int menu_shown = 0;

int game_cmd(char *cmd);
void game_clear(int x, int y, int w, int h)
{
	game_cursor(-1);
	if (game_theme.bg)
		gfx_draw_bg(game_theme.bg, x, y, w, h);
	else
		gfx_clear(x, y, w, h);

	if (menu_shown) {
		int xx = x - mx;
		int yy = y - my;
		gfx_draw_from(menubg, xx, yy, x, y, w, h); 
		gfx_draw_from(menu, xx, yy, x, y, w, h);
	//	gfx_update(mx, my, ww, hh);
		return;
	}
}

void game_clear(int x, int y, int w, int h);

struct el {
	int		id;
	int 		x;
	int 		y;
	int		mx; /* mouse pointer */
	int		my; /* coordinates */
	int 		type;
	int		drawn;
//	int 		clone;
	union {
		layout_t	lay;
		textbox_t	box;
		img_t		img;
		void		*p;
	} p;
};

enum {
	elt_box,
	elt_layout,
	elt_image,
};

enum {
	el_menu = 1,
	el_title,
	el_ways,
	el_inv,
	el_scene,
	el_sup, 
	el_sdown,
//	el_sslide, 
	el_iup, 
	el_idown,
//	el_islide, 
	el_spic, 
	el_menu_button,
	el_max,
};

struct el objs[el_max];

void 	el_set(int i, int t, int x, int y, void *p)
{
	objs[i].id = i;
	objs[i].x = x;
	objs[i].y = y;
	objs[i].p.p = p;
	objs[i].type = t;
	objs[i].drawn = 0;
//	objs[i].clone = 0;
}
void 	el_set_clone(int i, int t, int x, int y, void *p)
{
	el_set(i, t, x, y, p);
//	objs[i].clone = 1;
}

struct el *el(int num)
{
	return &objs[num];
}
textbox_t el_box(int num)
{
	return objs[num].p.box;
}

layout_t el_layout(int num)
{
	return objs[num].p.lay;
}

img_t el_img(int num)
{
	return objs[num].p.img;
}

char *game_menu_gen(void);

void game_menu(int nr)
{
	cur_menu = nr;
	game_menu_box(1, game_menu_gen());
}

int game_error(const char *name)
{
	game_done();
	if (game_init(NULL)) {
		fprintf(stderr,"Fatal error! Can't init anything!!!\n");
		exit(1);
	}
	game_menu(menu_error);
	return 0;
}

void el_draw(int n);

int window_sw = 0;
int fullscreen_sw = 0;

int game_load(int nr)
{
	char *s;
	s = game_save_path(0, nr);

	if (s && !access(s, R_OK)) {
		char cmd[PATH_MAX];
		snprintf(cmd, sizeof(cmd) - 1, "load %s", s);
		game_cmd(cmd);
		if (nr == -1)
			unlink(s);
		return 0;
	}
	return -1;
}

int game_save(int nr)
{
	char *s = game_save_path(1, nr);
	char cmd[PATH_MAX];
	char *p;
	if (s) {
		snprintf(cmd, sizeof(cmd) - 1, "save %s", s);
		p = instead_cmd(cmd);
		if (p)
			free(p);
		return 0;
	}
	return -1;
}

int game_apply_theme(void)
{
	layout_t lay;
	textbox_t box;

	memset(objs, 0, sizeof(struct el) * el_max);

	if (gfx_setmode(game_theme.w, game_theme.h, opt_fs))
		return -1;	
	gfx_bg(game_theme.bgcol);
	game_clear(0, 0, game_theme.w, game_theme.h);
	gfx_flip();
	lay = txt_layout(game_theme.font, ALIGN_JUSTIFY, game_theme.win_w, game_theme.win_h);
	if (!lay)
		return -1;
	box = txt_box(game_theme.win_w, game_theme.win_h);
	if (!box)
		return -1;
	txt_layout_color(lay, game_theme.fgcol);
	txt_layout_link_color(lay, game_theme.lcol);
	txt_layout_active_color(lay, game_theme.acol);

	txt_box_set(box, lay);
	el_set(el_scene, elt_box, game_theme.win_x, 0, box);

	lay = txt_layout(game_theme.inv_font, (game_theme.inv_mode == INV_MODE_HORIZ)?
			ALIGN_CENTER:ALIGN_LEFT, game_theme.inv_w, game_theme.inv_h);
	if (!lay)
		return -1;
	txt_layout_color(lay, game_theme.icol);
	txt_layout_link_color(lay, game_theme.ilcol);
	txt_layout_active_color(lay, game_theme.iacol);
	box = txt_box(game_theme.inv_w, game_theme.inv_h);
	if (!box)
		return -1;

	txt_box_set(box, lay);
	el_set(el_inv, elt_box, game_theme.inv_x, game_theme.inv_y, box);

	lay = txt_layout(game_theme.font, ALIGN_CENTER, game_theme.win_w, 0);
	if (!lay)
		return -1;

	txt_layout_color(lay, game_theme.fgcol);
	txt_layout_link_color(lay, game_theme.lcol);
	txt_layout_active_color(lay, game_theme.acol);

	el_set(el_title, elt_layout, game_theme.win_x, game_theme.win_y, lay);

	lay = txt_layout(game_theme.font, ALIGN_CENTER, game_theme.win_w, 0);
	if (!lay)
		return -1;
	
	txt_layout_color(lay, game_theme.fgcol);
	txt_layout_link_color(lay, game_theme.lcol);
	txt_layout_active_color(lay, game_theme.acol);
	
	el_set(el_ways, elt_layout, game_theme.win_x, 0, lay);

	el_set(el_sdown, elt_image, 0, 0, game_theme.a_down);
	el_set(el_sup, elt_image, 0, 0,  game_theme.a_up);
	el_set(el_idown, elt_image, 0, 0, game_theme.inv_a_down);
	el_set(el_iup, elt_image, 0, 0, game_theme.inv_a_up);

	el_set(el_spic, elt_image, game_theme.win_x, game_theme.win_y, NULL);
	el_set(el_menu, elt_layout, 0, 0, NULL);
	el_set(el_menu_button, elt_image, game_theme.menu_button_x, game_theme.menu_button_y, game_theme.menu_button);
	
	el_draw(el_menu_button);
	return 0;
}

int game_restart(void)
{
	char *og = curgame_dir;
	game_save(-1);
	game_done();
	if (game_init(og)) {
		game_error(og);
		return 0;
	}
	return 0;
}
int static cur_vol = 0;
void free_last_music(void);

void game_stop_mus(int ms)
{
	snd_stop_mus(ms);
	free_last_music();
}

int game_change_vol(int d, int val)
{
	int v = snd_volume_mus(-1);
	int pc = snd_vol_to_pcn(v);
	int opc = pc;
	if (d) {
		pc += d;
		if (pc < 0)
			pc = 0;
		if (pc > 100)
			pc = 100;
		while (snd_vol_to_pcn(v) != pc)
			v += (d<0)?-1:1;
	} else {
		v = val;
		pc = snd_vol_to_pcn(v);
	}
	if (!pc)
		v = 0;
	snd_volume_mus(v);
	if (opc && !pc) {
		game_stop_mus(0);
	} 
	if (!opc && pc) {
		game_music_player();
	}
	cur_vol = snd_volume_mus(-1);
	opt_vol = cur_vol;
	return 0;
}

int game_change_hz(int hz)
{
	if (!hz)
		return -1;
	snd_done();
	free_last_music();
	snd_init(hz);
	snd_volume_mus(cur_vol);
	snd_free_wav(game_theme.click);
	game_theme.click = snd_load_wav(game_theme.click_name);
	game_music_player();
	opt_hz = snd_hz();
	return 0;
}


int game_init(const char *name)
{
	getcwd(game_cwd, sizeof(game_cwd));
	
	if (!opt_lang || !opt_lang[0])
		opt_lang = game_locale();
		
	if (menu_lang_select(opt_lang) && menu_lang_select(LANG_DEF)) {
		fprintf(stderr, "Can not load default language.\n");
		exit(1);
	}	
	if (name)
		game_err_msg(NULL);

	if (gfx_init() || input_init())
		return -1;	

	snd_init(opt_hz);
	game_change_vol(0, opt_vol);

	if (game_default_theme()) {
		fprintf(stderr, "Can't load default theme.\n");
		return -1;
	}

	if (game_select(name))
		return -1;
	
	if (curgame_dir && !access(THEME_FILE, R_OK)) {
		game_own_theme = 1;
	}
	
	if (game_own_theme && opt_owntheme) {
		if (theme_load(THEME_FILE))
			return -1;
	} else if (curtheme_dir && strcmp(DEFAULT_THEME, curtheme_dir)) {
		game_theme_load(curtheme_dir);
	}

	if (game_apply_theme())
		return -1;

	if (!curgame_dir) {
		game_menu(menu_games);
	} else {
		if (!game_load(-1)) /* tmp save */
			return 0;
		if (opt_autosave && !game_load(0))  /* autosave */
			return 0;
		instead_eval("game:ini()");
		game_cmd("look");
		custom_theme_warn();
		if (opt_autosave)
			game_save(0);
	}
	return 0;
}

void free_last_music(void)
{
	if (last_music)
		free(last_music);
	last_music = NULL;
}

void free_last(void)
{
	if (last_pict)
		free(last_pict);
	if (last_title)
		free(last_title);
	last_pict = last_title = NULL;
	game_stop_mus(500);
}


void game_done(void)
{
	int i;
	if (opt_autosave && curgame_dir)
		game_save(0);
	chdir(game_cwd);
//	cfg_save();

	if (menu_shown)
		menu_toggle();
	mouse_reset();
	
	if (el_img(el_spic))
		gfx_free_image(el_img(el_spic));

	for (i = 0; i < el_max; i++) {
		struct el *o;
		o = el(i);
//		if (o->type == elt_image && o->p.p) {
//			if (!o->clone)
//				gfx_free_image(o->p.img);
//		} else 
		if (o->type == elt_layout && o->p.p) {
			txt_layout_free(o->p.lay);
		} else if (o->type == elt_box && o->p.p) {
			txt_layout_free(txt_box_layout(o->p.box));
			txt_box_free(o->p.box);
		}
		o->p.p = NULL;
		o->drawn = 0;
	}
	free_last();
	if (menu)
		gfx_free_image(menu);
	if (menubg)
		gfx_free_image(menubg);
	menu = menubg = NULL;
	game_theme_free();
	input_clear();
	snd_done();
	instead_done();
	gfx_done();
	curgame_dir = NULL;
	game_own_theme = 0;
//	SDL_Quit();
}	

void el_size(int i, int *w, int *h)
{
	int type;
	type = el(i)->type;
	if (type == elt_layout) 
		txt_layout_size(el_layout(i), w, h);
	else if (type == elt_box)
		txt_box_size(el_box(i), w, h);
	else if (type  == elt_image) {
		if (w)
			*w = gfx_img_w(el_img(i));
		if (h)
			*h = gfx_img_h(el_img(i));
	} 
}

int el_clear(int n)
{
	int x, y, w, h;
	struct el *o;
	o = el(n);
	if (!o || !o->drawn)
		return 0;
	x = o->x;
	y = o->y;
	el_size(n, &w, &h);
	o->drawn = 0;
	game_clear(x, y, w, h);
	return 1;
}

void el_update(int n)
{
	int x, y, w, h;
	struct el *o;
	o = el(n);
//	if (!o->drawn)
//		return;
	x = o->x;
	y = o->y;
	el_size(n, &w, &h);
	game_cursor(1);
	gfx_update(x, y, w, h);
	return;
}

void box_update_scrollbar(int n)
{
	struct el *elup = NULL;
	struct el *eldown = NULL;
//	struct el *elslide;
	layout_t l;

	int x1, y1;
	int x2, y2;

	int off;
	int w, h, hh;

	el_size(n, &w, &h);

	x1 = el(n)->x + w + game_theme.pad;
	y1 = el(n)->y;

	x2 = x1;
	y2 = y1 + h - gfx_img_h(game_theme.a_down);

	l = txt_box_layout(el_box(n));
	txt_layout_size(l, NULL, &hh);
	off = txt_box_off(el_box(n));
	if (n == el_scene) {
		elup = el(el_sup);
		eldown = el(el_sdown);
//		elslide = el(el_sslide);
	} else if (n == el_inv) {
		elup = el(el_iup);
		eldown = el(el_idown);
//		elslide = el(el_islide);
	}
	if (!elup || !eldown)
		return;	

	if (el_clear(elup->id)) {
		if (elup->x != x1 || elup->y != y1)
			el_update(elup->id);
	}

	if (el_clear(eldown->id)) {
		if (eldown->x != x2 || eldown->y != y2) 
			el_update(eldown->id);
	}

	elup->x = x1;
	elup->y = y1;
	eldown->x = x2;
	eldown->y = y2;
	
	el_clear(elup->id);
	el_clear(eldown->id);

	if (hh - off > h)
		el_draw(eldown->id);
	if (off)
		el_draw(elup->id);
	el_update(elup->id);
	el_update(eldown->id);
}

void el_draw(int n)
{
	int x, y;
	struct el *o;
	o = el(n);
	x = o->x;
	y = o->y;
	if (!o->p.p)
		return;
	game_cursor(-1);
	if (o->type == elt_image)
		gfx_draw(o->p.img, x, y);
	else if (o->type == elt_layout)
		txt_layout_draw(o->p.lay, x, y);
	else if (o->type == elt_box) {
		txt_box_draw(o->p.box, x, y);
		box_update_scrollbar(o->id);
	}
	o->drawn = 1;
	return;
}

img_t	game_pict_scale(img_t img, int ww, int hh)
{
	img_t img2 = img;
	int w, h, www, hhh;
	float scale1, scale2, scale = 1.0f;
	w = gfx_img_w(img);
	h = gfx_img_h(img);

	if (ww == -1)
		ww = w;
	if (hh == -1)
		hh = h;

	if (w <= ww && h <= hh)
		return img;
	
	www = ww;
	hhh = hh;
	
	while (scale * (float)w > ww || scale * (float)h > hh) {
		scale1 = (float)(www - 2) / (float)(w);
		scale2 = (float)(hhh - 2) / (float)(h);
		scale = (scale1<scale2) ? scale1:scale2;
		www -= 1;
		hhh -= 1;
		if (www <= 0 || hhh <=0)
			break;
	}
	
	img2 = gfx_scale(img, scale, scale);
	gfx_free_image(img);
	return img2;
}



void game_menu_box(int show, const char *txt)
{	
//	img_t	menu;
	int w, h, mw, mh;
	int x, y;
	int b = game_theme.border_w;
	int pad = game_theme.pad;
	layout_t lay;

	menu_shown = show;
	
	el(el_menu)->drawn = 0;
	if (el_layout(el_menu)) {
		txt_layout_free(el_layout(el_menu));
		el(el_menu)->p.p = NULL;
	}
	if (menubg) {
		game_cursor(-1);
		gfx_draw(menubg, mx, my);
		gfx_free_image(menubg);
		menubg = NULL;
	}

	el_clear(el_menu_button);
	if (!show)
		el_draw(el_menu_button);

//	el_update(el_menu_button);

	if (!show) {
		game_cursor(1);
		gfx_flip();
		return;
	}
	lay = txt_layout(game_theme.menu_font, ALIGN_CENTER, game_theme.win_w - 2 * (b + pad), 0);
	txt_layout_set(lay, (char*)txt);
	txt_layout_real_size(lay, &w, &h);	
	txt_layout_free(lay);

	lay = txt_layout(game_theme.menu_font, ALIGN_CENTER, w, 0);

	txt_layout_set(lay, (char*)txt);
	txt_layout_real_size(lay, &w, &h);	

	txt_layout_color(lay, game_theme.menu_fg);
	txt_layout_link_color(lay, game_theme.menu_link);
	txt_layout_active_color(lay, game_theme.menu_alink);
	txt_layout_set(lay, (char*)txt);
	txt_layout_real_size(lay, &w, &h);	
	if (menu) {
		gfx_free_image(menu);
		menu = NULL;
	}
	menu = gfx_new(w + (b + pad)*2, h + (b + pad)*2);
	gfx_img_fill(menu, 0, 0, w + (b + pad)*2, h + (b + pad)*2, game_theme.border_col);
	gfx_img_fill(menu, b, b, w + pad*2, h + pad*2, game_theme.menu_bg);
	gfx_set_alpha(menu, game_theme.menu_alpha);
	x = (game_theme.win_w - w)/2 + game_theme.win_x; //(game_theme.w - w)/2;
	y = (game_theme.win_h - h)/2 + game_theme.win_y; //(game_theme.h - h)/2;
	mx = x - b - pad;
	my = y - b - pad;
	mw = w + (b + pad) * 2;
	mh = h + (b + pad) * 2;
	game_cursor(-1);
	menubg = gfx_grab_screen(mx, my, mw, mh);
	gfx_draw(menu, mx, my);
	el_set(el_menu, elt_layout, /*game_theme.win_x*/  x, y, lay);
	el_draw(el_menu);
	game_cursor(1);
	gfx_flip();
}

int check_new_place(char *title)
{
	int rc = 0;
	if (!title && !last_title)
		return 0;

	if (!title && last_title) {
		rc = 1;
	} else if (!last_title || strcmp(title, last_title)) {
		rc = 1;
	}
	if (last_title) {
		free(last_title);
	}
	last_title = title;
	return rc;
}

int check_new_pict(char *pict)
{
	int rc = 0;
	if (!pict && !last_pict)
		return 0;

	if (!pict && last_pict) {
		rc = 1;
	} else if (!last_pict || strcmp(pict, last_pict)) {
		rc = 1;
	}
	if (last_pict) {
		free(last_pict);
	}
	last_pict = pict;
	return rc;
}

void scene_scrollbar(void)
{
	layout_t l;
	int h, off;
	int hh;
	el_clear(el_sdown);
	el_clear(el_sup);
	el_size(el_scene, NULL, &hh);
	el(el_sup)->y = el(el_scene)->y;
	l = txt_box_layout(el_box(el_scene));
	txt_layout_size(l, NULL, &h);
	off = txt_box_off(el_box(el_scene));
	if (h - off >= hh)
		el_draw(el_sdown);
	if (off)
		el_draw(el_sup);
}


static void dec_music(void *data)
{
	char *mus;
	if (!curgame_dir)
		return;
	mus = instead_eval("return dec_music_loop()");
	if (!mus)
		return;
	if (atoi(mus) == -1)
		free_last_music();
	free(mus);
}

void game_music_finished(void)
{
	push_user_event(&dec_music, NULL);
}

void game_music_player(void)
{
	int 	loop;
	char		*mus;
	if (!snd_volume_mus(-1))
		return;
	if (!opt_music)
		return;
		
	mus = instead_eval("return get_music_loop()");

	if (mus) {
		loop = atoi(mus);
		free(mus);
	} else
		loop = -1;
		
	mus = instead_eval("return get_music()");
	unix_path(mus);
	
	if (mus && loop == -1) { /* disabled, 0 - forever, 1-n - loops */
		free(mus);
		mus = NULL;
	}
	
	if (!mus) {
		if (last_music) {
			game_stop_mus(500);
		}
	} else if (!last_music && mus) {
		game_stop_mus(500);
		last_music = mus;
		snd_play_mus(mus, 0, loop - 1);
	} else if (strcmp(last_music, mus)) {
		game_stop_mus(500);
		last_music = mus;
		snd_play_mus(mus, 0, loop - 1);
	} else
		free(mus);
}

char *horiz_inv(char *invstr)
{
	char *p = invstr;
	char *ns = malloc(strlen(p) * 3);
	char *np = ns;
	if (!np)
		return invstr;
	while (*p) {
		if (*p == '\n') {
			if (p[strspn(p, " \n\t")]) {
				*(np++) = ' ';
				*(np++) = '|';
				*(np) = ' ';
			} else
				break;
		} else
			*np = *p;
		p ++;
		np ++;
	}
	*(np++) = '\n';
	*np = 0;
	free(invstr);
	invstr = ns;
	return invstr;
}

int game_cmd(char *cmd)
{
	int		new_pict = 0, new_place = 0;
	int		title_h = 0, ways_h = 0, pict_h = 0;
	char 		buf[1024];
	char 		*cmdstr;
	char 		*invstr;
	char 		*waystr;
	char		*title;
	char 		*pict;
	img_t		oldscreen = NULL;

	cmdstr = instead_cmd(cmd);
	if (!cmdstr) 
		goto err;
	game_music_player();	
//	sound_player(); /* TODO */
	title = instead_eval("return get_title();");
	unix_path(title);
	if (title) {
		snprintf(buf, sizeof(buf), "<b><c><a:look>%s</a></c></b>", title);
		txt_layout_set(el_layout(el_title), buf);
	} else
		txt_layout_set(el_layout(el_title), NULL);

	new_place = check_new_place(title);

	txt_layout_size(el_layout(el_title), NULL, &title_h);
	title_h += game_theme.font_size / 2; // todo?	
	pict = instead_eval("return get_picture();");

	new_pict = check_new_pict(pict);

	if (pict) {
		int w, h, x;
		img_t img;

		if (new_pict) {
			img = gfx_load_image(pict);
			if (el_img(el_spic))
				gfx_free_image(el_img(el_spic));
			el(el_spic)->p.p = NULL;
			if (game_theme.gfx_mode != GFX_MODE_FLOAT) 
				img = game_pict_scale(img, game_theme.win_w, game_theme.max_scene_h);
			else
				img = game_pict_scale(img, game_theme.max_scene_w, game_theme.max_scene_h);
		} else
			img = el_img(el_spic);

		if (img) {
			w = gfx_img_w(img);
			h = gfx_img_h(img);
			if (game_theme.gfx_mode != GFX_MODE_FLOAT) {
				x = (game_theme.win_w - w)/2 + game_theme.win_x;
				el_set(el_spic, elt_image, x, game_theme.win_y + title_h, img);
			} else {
				x = (game_theme.max_scene_w - w)/2 + game_theme.gfx_x;
				el_set(el_spic, elt_image, x, game_theme.gfx_y/* + (game_theme.max_scene_h - h)/2*/, img);
			}
//			if (!game_theme.emb_gfx)
			pict_h = h;
		}
	} else if (el_img(el_spic)) {
		if (game_theme.gfx_mode != GFX_MODE_EMBEDDED)
			el_clear(el_spic);
		gfx_free_image(el_img(el_spic));
		el(el_spic)->p.p = NULL;
	}

	waystr = instead_cmd("way");
	invstr = instead_cmd("inv");

	if (invstr && game_theme.inv_mode == INV_MODE_HORIZ) {
		invstr = horiz_inv(invstr);
	}

	if (waystr) {
		waystr[strcspn(waystr,"\n")] = 0;
	}

	if (game_theme.gfx_mode != GFX_MODE_EMBEDDED) {
		txt_layout_set(el_layout(el_ways), waystr);
		txt_layout_size(el_layout(el_ways), NULL, &ways_h);
	} 


	if (game_theme.gfx_mode == GFX_MODE_EMBEDDED) {
		int off = 0;
		if (!new_pict && !new_place) {
			off = txt_box_off(el_box(el_scene));
			if (off > pict_h)
				off = pict_h;
		}
		pict_h = 0; /* to fake code bellow */
		txt_layout_set(txt_box_layout(el_box(el_scene)), ""); /* hack, to null layout, but not images */
		if (el_img(el_spic)) {
			txt_layout_add_img(txt_box_layout(el_box(el_scene)),"scene", el_img(el_spic));
			txt_layout_add(txt_box_layout(el_box(el_scene)), "<c><g:scene></c>\n");
		}
		txt_layout_add(txt_box_layout(el_box(el_scene)), waystr);
		txt_layout_add(txt_box_layout(el_box(el_scene)), "<l>\n"); /* small hack */
		txt_layout_add(txt_box_layout(el_box(el_scene)), cmdstr);
		txt_box_set(el_box(el_scene), txt_box_layout(el_box(el_scene)));
		if (!new_pict && !new_place) 
			txt_box_scroll(el_box(el_scene), off);
	} else {
		if (game_theme.gfx_mode == GFX_MODE_FLOAT) 
			pict_h = 0;	
		txt_layout_set(txt_box_layout(el_box(el_scene)), cmdstr);
		txt_box_set(el_box(el_scene), txt_box_layout(el_box(el_scene)));
	}
	free(cmdstr);
	el(el_ways)->y = el(el_title)->y + title_h + pict_h;
	if (waystr)
		free(waystr);

	el(el_scene)->y = el(el_ways)->y + ways_h;
	
	/* draw title and ways */
	if (new_pict || new_place) {
		img_t offscreen = gfx_new(game_theme.w, game_theme.h);
		oldscreen = gfx_screen(offscreen);
		gfx_draw(oldscreen, 0, 0);
	}
	if (new_pict || new_place) {
		game_clear(game_theme.win_x, game_theme.win_y, game_theme.win_w, game_theme.win_h);
		if (game_theme.gfx_mode == GFX_MODE_FLOAT) {
			game_clear(game_theme.gfx_x, game_theme.gfx_y, game_theme.max_scene_w, game_theme.max_scene_h);
		}
//		el_draw(el_title);
	} else {
		game_clear(game_theme.win_x, game_theme.win_y + pict_h + title_h, 
			game_theme.win_w, game_theme.win_h - pict_h - title_h);
	}
	
	el_clear(el_title);
	el_draw(el_title);

	if (game_theme.gfx_mode != GFX_MODE_EMBEDDED) {
		el_draw(el_ways);
		if ((new_pict || new_place))
			el_draw(el_spic);
	}
	
	txt_box_resize(el_box(el_scene), game_theme.win_w, game_theme.win_h - title_h - ways_h - pict_h);
	el_draw(el_scene);

	do {
		int off = txt_box_off(el_box(el_inv));
		txt_layout_set(txt_box_layout(el_box(el_inv)), invstr);
		txt_box_set(el_box(el_inv), txt_box_layout(el_box(el_inv)));
		txt_box_scroll(el_box(el_inv), off);
	} while(0);
	
	if (invstr)
		free(invstr);
	
	el_clear(el_inv);
	el_draw(el_inv);
//	scene_scrollbar();
	if (new_pict || new_place) {
		img_t offscreen;
		offscreen = gfx_screen(oldscreen);
		gfx_change_screen(offscreen);
		gfx_free_image(offscreen);
//		input_clear();
		goto err;
	}
	gfx_flip();
//	input_clear();
err:
	if (err_msg) {
		game_menu(menu_warning);
		return -1;
	}
	return 0;
}

void game_update(int x, int y, int w, int h)
{
	game_cursor(1);
	gfx_update(x, y, w, h);
}

void game_xref_update(xref_t xref, int x, int y)
{
	game_cursor(-1);
	xref_update(xref, x, y, game_clear, game_update);
	game_cursor(1);
}

xref_t	inv_xref = NULL;

int disable_inv(void)
{
	if (inv_xref) {
		xref_set_active(inv_xref, 0);
		game_xref_update(inv_xref, el(el_inv)->x, el(el_inv)->y);
//		txt_box_update_links(el_box(el_inv), el(el_inv)->x, el(el_inv)->y, game_clear);
		inv_xref = NULL;
		return 1;
	}
	return 0;
}

void enable_inv(xref_t xref)
{
	inv_xref = xref;
	xref_set_active(xref, 1);
	//txt_box_update_links(el_box(el_inv), el(el_inv)->x, el(el_inv)->y, game_clear);
	game_xref_update(inv_xref, el(el_inv)->x, el(el_inv)->y);
}


struct el *look_obj(int x, int y)
{
	int i;
	for (i = 0; i < el_max; i++) {
		int w, h;
		
		if (el(i)->drawn && el(i)->id == el_menu) {
			return el(i);
		}
		if (x < el(i)->x || y < el(i)->y || !el(i)->drawn)
			continue;
		el_size(i, &w, &h);
		if (x >= el(i)->x && y >= el(i)->y && x <= el(i)->x + w && y <= el(i)->y + h)
			return el(i);
	}
	return NULL;
}

xref_t look_xref(int x, int y, struct el **elem)
{
	struct el *o;
	int type;
	xref_t xref = NULL;
	o = look_obj(x, y);
	if (elem)
		*elem = o;
	if (!o)
		return NULL;
	type = o->type;
	if (type == elt_layout) 
		xref = txt_layout_xref(o->p.lay, x - o->x, y - o->y);
	else if (type == elt_box)
		xref = txt_box_xref(o->p.box, x - o->x, y - o->y);
	return xref;
}

static xref_t old_xref = NULL;
static struct el *old_el = NULL;

void menu_update(struct el *elem)
{
	gfx_draw(menubg, mx, my);
	gfx_draw(menu, mx, my);
	txt_layout_draw(elem->p.lay, elem->x, elem->y);
	gfx_update(mx, my, gfx_img_w(menu), gfx_img_h(menu));
//	gfx_fill(x, y, w, h, game_theme.menu_bg);
}


int game_highlight(int x, int y, int on)
{
	struct el 	 *elem = NULL;
	static struct el *oel = NULL;
	static xref_t 	 hxref = NULL;
	xref_t		xref = NULL;
	int up = 0;
	
	if (on) {
		xref = look_xref(x, y, &elem);
		if (xref && opt_hl) {
			xref_set_active(xref, 1);
			game_xref_update(xref, elem->x, elem->y);
		}
	}
	
	if (hxref != xref && oel) {
		if (hxref != inv_xref) {
			xref_set_active(hxref, 0);
			game_xref_update(hxref, oel->x, oel->y);
			up = 1;
		}
		hxref = NULL;
	}
	hxref = xref;
	oel = elem;
	return 0;
}

static void mouse_reset(void)
{
	game_highlight(-1, -1, 0);
	disable_inv();
	motion_mode = 0;
	old_xref = old_el = NULL;
}

static void menu_toggle(void)
{
	menu_shown ^= 1;
	if (!menu_shown)
		cur_menu = menu_main;
	mouse_reset();
	game_menu_box(menu_shown, game_menu_gen());
}

static void scroll_pup(int id)
{
	game_highlight(-1, -1, 0);
	if (game_theme.gfx_mode == GFX_MODE_EMBEDDED) {
		int hh;
		el_size(el_scene, NULL, &hh);
		txt_box_scroll(el_box(id), -hh);
	} else
		txt_box_prev(el_box(id));
	el_clear(id);
	el_draw(id);
	el_update(id);
}

static void scroll_pdown(int id)
{
	game_highlight(-1, -1, 0);
	if (game_theme.gfx_mode == GFX_MODE_EMBEDDED) {
		int hh;
		el_size(el_scene, NULL, &hh);
		txt_box_scroll(el_box(id), hh);
	} else
		txt_box_next(el_box(id));
	el_clear(id);
	el_draw(id);
	el_update(id);
}

extern unsigned int timer_counter;

int mouse_filter(void)
{
	static unsigned int old_counter = 0;
	if (!opt_filter)
		return 0;
	if (abs(old_counter - timer_counter) <= 4) /* 400 ms */
		return -1;
	old_counter = timer_counter;
	return 0;
}

int game_click(int x, int y, int action)
{
	struct el	*elem = NULL;
	char 		buf[1024];
	xref_t 		xref = NULL;

	if (action)
		motion_mode = 0;

	if (opt_filter && action) {
		xref_t new_xref;
		struct el *new_elem;
		new_xref = look_xref(x, y, &new_elem);
		if (new_xref != old_xref || new_elem != old_el) {
			old_el = NULL;
			if (old_xref) {
				old_xref = NULL;
				return 0; /* just filtered */
			}
			old_xref = NULL;
		}
	}
	if (action) {
		xref = old_xref;
		elem = old_el;
		old_xref = NULL;
		old_el = NULL;
	} else  { /* just press */
		xref = look_xref(x, y, &elem);
		if (xref) {
			xref_set_active(xref, 1);
			game_xref_update(xref, elem->x, elem->y);
		} else if (elem && elem->type == elt_box && opt_motion) {
			motion_mode = 1;
			motion_id = elem->id;
			motion_y =y;
			return 0;
		}
		old_xref = xref;
		old_el = elem;
		return 0;
	}
	/* now look only second press */
	
	if (!xref) {
		if (elem) {
			if (elem->id == el_menu_button) {
				menu_toggle();
			} else if (elem->id == el_sdown) {
				scroll_pdown(el_scene);
			} else if (elem->id == el_sup) {
				scroll_pup(el_scene);
			} else if (elem->id == el_idown) {
				scroll_pdown(el_inv);
			} else if (elem->id == el_iup) {
				scroll_pup(el_inv);
			} else if (disable_inv())
				el_update(el_inv);
			motion_mode = 0;
		} else if (disable_inv()) {
			el_update(el_inv);
//			gfx_flip();
		}
		return 0;
	}
/* second xref */
	if (elem->id == el_menu) {
//		xref_set_active(xref, 0);
//		txt_layout_update_links(elem->p.lay, elem->x, elem->y, game_clear);
		if (game_menu_act(xref_get_text(xref))) {
			return -1;
		}
//		game_menu_box(menu_shown, game_menu_gen());
//		gfx_flip();
		return 1;
	}

	if (elem->id == el_ways ||
		elem->id == el_title) {
		strcpy(buf, xref_get_text(xref));
		if (mouse_filter())
			return 0;
		if (opt_click)
			snd_play(game_theme.click);
		if (disable_inv()) {
			el_update(el_inv);
			return 0;
		}
		game_cmd(buf);
		return 1;
	}

	if (elem->id == el_scene) {
		if (inv_xref) {
			snprintf(buf,sizeof(buf), "use %s,%s", xref_get_text(inv_xref), xref_get_text(xref));
			disable_inv();
		} else	
			strcpy(buf, xref_get_text(xref));
		if (mouse_filter())
			return 0;
		if (opt_click)
			snd_play(game_theme.click);
		game_cmd(buf);
		return 1;
	}
	
	if (elem->id == el_inv) {
		if (!inv_xref) {
			enable_inv(xref);
			el_update(el_inv);
			return 0;
		}	
		if (xref == inv_xref)
			snprintf(buf,sizeof(buf), "use %s", xref_get_text(xref));
		else
			snprintf(buf,sizeof(buf), "use %s,%s", xref_get_text(inv_xref), xref_get_text(xref));
		disable_inv();
		if (mouse_filter())
			return 0;
		if (opt_click)
			snd_play(game_theme.click);
		game_cmd(buf);
		return 1;
	}
	return 0;
}

static void game_cursor(int on)
{
	static img_t	grab = NULL;
	static img_t 	cur;
	static int xc = 0, yc = 0, ow = 0, oh = 0; //, w, h;
	if (grab) {
		gfx_draw(grab, xc, yc);
		gfx_free_image(grab);
		grab = NULL;
		if (!on) {
			gfx_update(xc, yc, ow, oh);
			return;
		}
	}

	if (on == -1)
		return;
	
	cur = (inv_xref) ? game_theme.use:game_theme.cursor;
	
	if (!cur)
		return;	

	do {
		int ox = xc;
		int oy = yc;
		gfx_cursor(&xc, &yc, NULL, NULL);
		xc -= game_theme.cur_x;
		yc -= game_theme.cur_y;
//		xc += w/2;
//		yc += h/2;
		grab = gfx_grab_screen(xc, yc, gfx_img_w(cur), gfx_img_h(cur));
		gfx_draw(cur, xc, yc);
		gfx_update(xc, yc, gfx_img_w(cur), gfx_img_h(cur));
		gfx_update(ox, oy, ow, oh);
		ow = gfx_img_w(cur);
		oh = gfx_img_h(cur);
	} while (0);
}


static void scroll_up(int id, int count)
{
	int i;
	game_highlight(-1, -1, 0);
	if (game_theme.gfx_mode == GFX_MODE_EMBEDDED)
		txt_box_scroll(el_box(id), -(FONT_SZ(game_theme.font_size)) * count);
	else
		for (i = 0; i < count; i++)
			txt_box_prev_line(el_box(id));
	el_clear(id);
	el_draw(id);
	el_update(id);
}

static void scroll_down(int id, int count)
{
	int i;
	game_highlight(-1, -1, 0);
	if (game_theme.gfx_mode == GFX_MODE_EMBEDDED)
		txt_box_scroll(el_box(id), (FONT_SZ(game_theme.font_size)) * count);
	else
		for (i = 0; i < count; i++)
			txt_box_next_line(el_box(id));
	el_clear(id);
	el_draw(id);
	el_update(id);
}

static void scroll_motion(int id, int off)
{
	game_highlight(-1, -1, 0);
	txt_box_scroll(el_box(id), off);
	el_clear(id);
	el_draw(id);
	el_update(id);
}


static int sel_el = 0;

static void frame_next(void)
{
	switch(sel_el) {
	default:
	case 0:
		sel_el = el_scene;
		break;
	case el_ways:
		sel_el = el_scene;
		break;
	case el_scene:
		sel_el = el_inv;
		break;
	case el_inv:
		if (game_theme.gfx_mode != GFX_MODE_EMBEDDED && 
			txt_layout_xrefs(el_layout(el_ways)))
			sel_el = el_ways;
		else
			sel_el = el_scene;
		break;
	}
}

static void frame_prev(void)
{
	switch(sel_el) {
	default:
	case 0:
		sel_el = el_inv;
		break;
	case el_title:
		sel_el = el_inv;
		break;
	case el_ways:
		sel_el = el_inv;
		break;
	case el_scene:
		if (game_theme.gfx_mode != GFX_MODE_EMBEDDED && 
			txt_layout_xrefs(el_layout(el_ways)))
			sel_el = el_ways;
		else
			sel_el = el_inv;
		break;
	case el_inv:
		sel_el = el_scene;
		break;
	}
}

static void select_ref(int prev);
static xref_t get_xref(int i, int last);
static void xref_jump(xref_t xref, struct el* elem);

static xref_t get_nearest_xref(int i, int mx, int my);

static void select_frame(int prev)
{
	struct el *elem = NULL;
	int x, y, w, h;
	
	gfx_cursor(&x, &y, NULL, NULL);
	
	elem = look_obj(x, y);
	
	if (elem)
		sel_el = elem->id;
	
	el(sel_el)->mx = x;
	el(sel_el)->my = y;

	if (menu_shown) {
		sel_el = el_menu;
	} else {
//		int old_sel;
//		if (!sel_el)
//			frame_next();
//		old_sel = sel_el;
//		do {
			if (prev) {
				frame_prev();
			} else {
				frame_next();
			}
//		} while (!get_xref(sel_el, 0) && sel_el != old_sel);
	}
	el_size(sel_el, &w, &h);
	x = el(sel_el)->mx;
	y = el(sel_el)->my;
	if (x < el(sel_el)->x || y < el(sel_el)->y || 
		x > el(sel_el)->x + w || y > el(sel_el)->y + h) {
		x = el(sel_el)->x + w / 2;
		y = el(sel_el)->y + h / 2;
	}
	
	gfx_warp_cursor(x, y);
	
	if (!look_xref(x, y, &elem) && elem) {
		xref_t xref = get_nearest_xref(elem->id, x, y);
		xref_jump(xref, elem);
	}	
}

static int xref_rel_position(xref_t xref, struct el *elem, int *x, int *y)
{
	int rc = xref_position(xref, x, y);	
	if (!rc && elem->type == elt_box && y) {
		*y -= txt_box_off(el_box(elem->id));
	}
	return rc;
}

static int xref_visible(xref_t xref, struct el *elem)
{
	int x, y, w, h;
	if (!elem || !xref)
		return -1;
		
	xref_rel_position(xref, elem, &x, &y);	
	el_size(elem->id, &w, &h);
	if (y < 0 || y >= h)
		return -1;
	return 0;
}

static xref_t get_nearest_xref(int i, int mx, int my)
{
	xref_t		xref = NULL;
	xref_t		min_xref = NULL;
	int min_disp = game_theme.h * game_theme.h + game_theme.w * game_theme.w;
	if (!i)
		return NULL;
	for (xref = get_xref(i, 0); !xref_visible(xref, el(i)); xref = xref_next(xref)) {
		int x, y, disp;
		xref_rel_position(xref, el(i), &x, &y);
		disp = (x + el(i)->x - mx) * (x + el(i)->x - mx) + (y + el(i)->y - my) * (y + el(i)->y - my);
		if (disp < min_disp) {
			min_disp = disp;
			min_xref = xref;
		}
	}
	return min_xref;
}

static xref_t get_xref(int i, int last)
{
	xref_t		xref = NULL;
	int type;
	type = el(i)->type;
	if (type == elt_layout) {
		xref = txt_layout_xrefs(el_layout(i));
		while (last && xref && xref_next(xref))
			xref = xref_next(xref);
	} else if (type == elt_box) {
		xref = txt_box_xrefs(el_box(i));
		while (last && xref && !xref_visible(xref_next(xref), el(i)))
			xref = xref_next(xref);
	}
	return xref;
}

static void xref_jump(xref_t xref, struct el* elem)
{
	int x, y;
	if (!elem || !xref || xref_rel_position(xref, elem, &x, &y))
		return;
	gfx_warp_cursor(elem->x + x, elem->y + y);
}

static void select_ref(int prev)
{
	int x, y;
	struct el 	 *elem = NULL;
	xref_t		xref = NULL;
	gfx_cursor(&x, &y, NULL, NULL);
	
	xref = look_xref(x, y, &elem);
	
	if (!elem) {
		if (!sel_el)
			select_frame(0);
		elem = el(sel_el);
	}
	
	if (xref) {
		if (prev) {
			if (!(xref = xref_prev(xref)) || xref_visible(xref, elem))
				xref = get_xref(elem->id, 1);
		} else {
			if (!(xref = xref_next(xref)) || xref_visible(xref, elem))
				xref = get_xref(elem->id, 0);
		}
	} 
	
	if (!xref)
		xref = get_nearest_xref(elem->id, x, y);

	xref_jump(xref, elem);		
}

static void game_scroll_up(int count)
{
	int xm, ym;
	struct el *o;
	gfx_cursor(&xm, &ym, NULL, NULL);
	o = look_obj(xm, ym);
	if (o && (o->id == el_scene || o->id == el_inv)) {
		scroll_up(o->id, count);
	}
}

static void game_scroll_down(int count)
{
	int xm, ym;
	struct el *o;
	gfx_cursor(&xm, &ym, NULL, NULL);
	o = look_obj(xm, ym);
	if (o && (o->id == el_scene || o->id == el_inv)) {
		scroll_down(o->id, count);
	}
}

static void game_scroll_pup(void)
{
	int xm, ym;
	struct el *o;
	gfx_cursor(&xm, &ym, NULL, NULL);
	o = look_obj(xm, ym);
	if (o && (o->id == el_scene || o->id == el_inv)) {
		scroll_pup(o->id);
	}
}

static void game_scroll_pdown(void)
{
	int xm, ym;
	struct el *o;
	gfx_cursor(&xm, &ym, NULL, NULL);
	o = look_obj(xm, ym);
	if (o && (o->id == el_scene || o->id == el_inv)) {
		scroll_pdown(o->id);
	}
}

static int is_key(struct inp_event *ev, const char *name)
{
	if (!ev->sym)
		return -1;
	return strcmp(ev->sym, name);
}

int game_loop(void)
{
	static int alt_pressed = 0;
	static int shift_pressed = 0;
	static int x = 0, y = 0;
	struct inp_event ev;
	memset(&ev, 0, sizeof(struct inp_event));
	while (1) {
		int rc;
		ev.x = -1;
		game_cursor(-1); /* release bg */
		while ((rc = input(&ev, 1)) == AGAIN);
		if (rc == -1) {/* close */
			break;
		} else if (((ev.type ==  KEY_DOWN) || (ev.type == KEY_UP)) && 
			(!is_key(&ev, "left alt") || !is_key(&ev, "right alt"))) {
			alt_pressed = (ev.type == KEY_DOWN) ? 1:0;
		} else if (((ev.type ==  KEY_DOWN) || (ev.type == KEY_UP)) && 
			(!is_key(&ev,"left shift") || !is_key(&ev, "right shift"))) {
			shift_pressed = (ev.type == KEY_DOWN) ? 1:0;
		} else if (ev.type == KEY_DOWN) {
			if (!alt_pressed && !is_key(&ev, "return")) {
				game_highlight(-1, -1, 0);
				gfx_cursor(&x, &y, NULL, NULL);
				game_click(x, y, 0);
				if (game_click(x, y, 1) == -1)
					break;
			} else if (!is_key(&ev, "escape")) {
				menu_toggle();
			} else if (!is_key(&ev, "tab")) {
				select_frame(shift_pressed);
			} else if (!is_key(&ev, "up")) {
				if (menu_shown || (!alt_pressed && !shift_pressed)) {
					select_ref(1);
				} else
					game_scroll_up(1);
			} else if (!is_key(&ev, "down")) {
				if (menu_shown || (!alt_pressed && !shift_pressed)) {
					select_ref(0);
				} else
					game_scroll_down(1);
			} else if (!is_key(&ev, "left")) {
				select_ref(1);
			} else if (!is_key(&ev, "right")) {
				select_ref(0);
			} else if (!is_key(&ev, "backspace") && !menu_shown) {
				scroll_pup(el_scene);
			} else if (!is_key(&ev, "space") && !menu_shown) {
				scroll_pdown(el_scene);
			} else if (!is_key(&ev, "page up") && !menu_shown) {
				game_scroll_pup();
			} else if (!is_key(&ev, "page down") && !menu_shown) {
				game_scroll_pdown();
			} else if (alt_pressed && !is_key(&ev, "q")) {
				break;
			} else if (alt_pressed &&
				(!is_key(&ev, "enter") || !is_key(&ev, "return"))) {
				int old_menu = (menu_shown) ? cur_menu: -1;
				shift_pressed = alt_pressed = 0;
				opt_fs ^= 1;
				game_restart();
				if (old_menu != -1)
					game_menu(old_menu);
			}
		} else if (ev.type == MOUSE_DOWN) {
			game_highlight(-1, -1, 0);
			game_click(ev.x, ev.y, 0);
			x = ev.x;
			y = ev.y;
		} else if (ev.type == MOUSE_UP) {
			game_highlight(-1, -1, 0);
			if (game_click(ev.x, ev.y, 1) == -1)
				break;
		} else if (ev.type == MOUSE_WHEEL_UP && !menu_shown) {
			game_scroll_up(ev.count);
		} else if (ev.type == MOUSE_WHEEL_DOWN && !menu_shown) {
			game_scroll_down(ev.count);
		} else if (ev.type == MOUSE_MOTION) {
			if (motion_mode) {
				scroll_motion(motion_id, motion_y - ev.y);
				motion_y = ev.y;
			}
		//	game_highlight(ev.x, ev.y, 1);
		}
		if (old_xref)
			game_highlight(x, y, 1);
		else {
			int x, y;
			gfx_cursor(&x, &y, NULL, NULL);
			game_highlight(x, y, 1);
		}
		game_cursor(1);
	}
	return 0;
}

