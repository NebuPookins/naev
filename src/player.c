

#include "player.h"

#include <malloc.h>

#include "xml.h"

#include "main.h"
#include "pilot.h"
#include "log.h"
#include "opengl.h"
#include "pack.h"
#include "space.h"
#include "rng.h"


#define XML_GUI_ID	"GUIs"   /* XML section identifier */
#define XML_GUI_TAG	"gui"

#define XML_START_ID	"Start"

#define GUI_DATA	"dat/gui.xml"
#define GUI_GFX	"gfx/gui/"

#define START_DATA "dat/start.xml"


#define pow2(x)	((x)*(x))


/* flag defines */
#define PLAYER_TURN_LEFT	(1<<0)	/* player is turning left */
#define PLAYER_TURN_RIGHT	(1<<1)	/* player is turning right */
#define PLAYER_FACE			(1<<2)	/* player is facing target */
#define PLAYER_PRIMARY		(1<<3)	/* player is shooting primary weapon */
#define PLAYER_SECONDARY	(1<<4)	/* player is shooting secondary weapon */
/* flag functions */
#define player_isFlag(f)	(player_flags & f)
#define player_setFlag(f)	(player_flags |= f)
#define player_rmFlag(f)	(player_flags ^= f)


#define KEY_PRESS		( 1.)
#define KEY_RELEASE	(-1.)
/* keybinding structure */
typedef struct {
	char *name; /* keybinding name, taken from keybindNames */
	KeybindType type; /* type, defined in playe.h */
	unsigned int key; /* key/axis/button event number */
	double reverse; /* 1. if normal, -1. if reversed, only useful for joystick axis */
} Keybind;
static Keybind** player_input; /* contains the players keybindings */
/* name of each keybinding */
const char *keybindNames[] = { "accel", "left", "right", /* movement */
		"primary", "target", "target_nearest", "face", "board", /* fighting */
		"secondary", "secondary_next", /* secondary weapons */
		"mapzoomin", "mapzoomout", "screenshot",  /* misc */
		"end" }; /* must terminate in "end" */


/*
 * player stuff
 */
Pilot* player = NULL; /* ze player */
unsigned int credits = 0;
static unsigned int player_flags = 0; /* player flags */
static double player_turn = 0.; /* turn velocity from input */
static double player_acc = 0.; /* accel velocity from input */
static unsigned int player_target = PLAYER_ID; /* targetted pilot */
static int planet_target = -1; /* targetted planet */


/*
 * pilot stuff for GUI
 */
extern Pilot** pilot_stack;
extern int pilots;


/*
 * GUI stuff
 */
/* standard colors */
glColour cConsole			=	{ .r = 0.5, .g = 0.8, .b = 0.5, .a = 1. };

glColour cInert			=	{ .r = 0.6, .g = 0.6, .b = 0.6, .a = 1. };
glColour cNeutral			=	{ .r = 0.9, .g = 1.0, .b = 0.3, .a = 1. };
glColour cFriend			=	{ .r = 0.0, .g = 1.0, .b = 0.0, .a = 1. };
glColour cHostile			=	{ .r = 0.9, .g = 0.2, .b = 0.2, .a = 1. };

glColour cRadar_player	=  { .r = 0.4, .g = 0.8, .b = 0.4, .a = 1. };
glColour cRadar_targ		=	{ .r = 0.0,	.g = 0.7, .b = 1.0, .a = 1. };
glColour cRadar_weap		=	{ .r = 0.8, .g = 0.2, .b = 0.2, .a = 1. };

glColour cShield			=	{ .r = 0.2, .g = 0.2, .b = 0.8, .a = 1. };
glColour cArmour			=	{ .r = 0.5, .g = 0.5, .b = 0.5, .a = 1. };
glColour cEnergy			=	{ .r = 0.2, .g = 0.8, .b = 0.2, .a = 1. };
typedef struct {
	double w,h; /* dimensions */
	RadarShape shape;
	double res; /* resolution */
} Radar;
/* radar resolutions */
#define RADAR_RES_MAX		100.
#define RADAR_RES_MIN		10.
#define RADAR_RES_INTERVAL	10.
#define RADAR_RES_DEFAULT	40.

typedef struct {
	double w,h;
} Rect;

typedef struct {
	/* graphics */
	gl_font smallFont;
	gl_texture* gfx_frame;
	gl_texture* gfx_targetPilot, *gfx_targetPlanet;
	Radar radar;
	Rect nav;
	Rect shield, armour, energy;
	Rect weapon;
	Rect misc;
	

	/* positions */
	Vector2d pos_frame;
	Vector2d pos_radar;
	Vector2d pos_nav;
	Vector2d pos_shield, pos_armour, pos_energy;
	Vector2d pos_weapon;
	Vector2d pos_target, pos_target_health, pos_target_name, pos_target_faction;
	Vector2d pos_misc;
	Vector2d pos_mesg;

} GUI;
GUI gui; /* ze GUI */
/* needed to render properly */
double gui_xoff = 0.;
double gui_yoff = 0.;

/* messages */
#define MESG_SIZE_MAX	80
int mesg_timeout = 3000;
int mesg_max = 5; /* maximum messages onscreen */
typedef struct {
	char str[MESG_SIZE_MAX];
	unsigned int t;
} Mesg;
static Mesg* mesg_stack;


/* 
 * prototypes
 */
/* external */
extern void pilot_render( const Pilot* pilot ); /* from pilot.c */
extern void weapon_minimap( const double res, const double w, const double h,
		const RadarShape shape ); /* from weapon.c */
extern void planets_minimap( const double res, const double w, const double h,
		const RadarShape shape ); /* from space.c */
/* internal */
static void rect_parse( const xmlNodePtr parent,
		double *x, double *y, double *w, double *h );
static int gui_parse( const xmlNodePtr parent, const char *name );
static void gui_renderPilot( const Pilot* p );
static void gui_renderBar( const glColour* c, const Vector2d* p,
		const Rect* r, const double w );
/* keybind actions */
static void player_board (void);
static void player_secondaryNext (void);
static void player_screenshot (void);



/*
 * creates a new player
 */
void player_new (void)
{
	Ship *ship;
	char system[20];
	uint32_t bufsize;
	char *buf = pack_readfile( DATA, START_DATA, &bufsize );
	int l,h;
	double x,y,d;
	Vector2d v;

	xmlNodePtr node, cur, tmp;
	xmlDocPtr doc = xmlParseMemory( buf, bufsize );

	node = doc->xmlChildrenNode;
	if (!xml_isNode(node,XML_START_ID)) {
		ERR("Malformed '"START_DATA"' file: missing root element '"XML_START_ID"'");
		return;
	}

	node = node->xmlChildrenNode; /* first system node */
	if (node == NULL) {
		ERR("Malformed '"START_DATA"' file: does not contain elements");
		return;
	}
	do {
		if (xml_isNode(node, "player")) { /* we are interested in the player */
			cur = node->children;
			do {
				if (xml_isNode(cur,"ship")) ship = ship_get( xml_get(cur) );
				else if (xml_isNode(cur,"credits")) { /* monies range */
					tmp = cur->children;
					do { 
						if (xml_isNode(tmp,"low")) l = xml_getInt(tmp);
						else if (xml_isNode(tmp,"high")) h = xml_getInt(tmp);
					} while ((tmp = tmp->next));
				}
				else if (xml_isNode(cur,"system")) {
					tmp = cur->children;
					do {
						/* system name, TODO percent chance */
						if (xml_isNode(tmp,"name")) snprintf(system,20,xml_get(tmp));
						/* position */
						else if (xml_isNode(tmp,"x")) x = xml_getFloat(tmp);
						else if (xml_isNode(tmp,"y")) y = xml_getFloat(tmp);
					} while ((tmp = tmp->next));
				}
			} while ((cur = cur->next));
		}
	} while ((node = node->next));

	xmlFreeDoc(doc);
	free(buf);
	xmlCleanupParser();


	/* monies */
	credits = RNG(l,h);

	/* pos and dir */
	vect_cset( &v, x, y );
	d = RNG(0,359)/180.*M_PI;

	pilot_create( ship, "Player", faction_get("Player"), NULL,
			d,  &v, NULL, PILOT_PLAYER );
	gl_bindCamera( &player->solid->pos );
	space_init(system);

	/* welcome message */
	player_message( "Welcome to "APPNAME"!" );
	player_message( " v%d.%d.%d", VMAJOR, VMINOR, VREV );
}


/*
 * adds a mesg to the queue to be displayed on screen
 */
void player_message ( const char *fmt, ... )
{
	va_list ap;
	int i;

	if (fmt == NULL) return; /* message not valid */

	/* copy old messages back */
	for (i=1; i<mesg_max; i++)
		if (mesg_stack[mesg_max-i-1].str[0] != '\0') {
			strcpy(mesg_stack[mesg_max-i].str, mesg_stack[mesg_max-i-1].str);
			mesg_stack[mesg_max-i].t = mesg_stack[mesg_max-i-1].t;
		}

	/* add the new one */
	va_start(ap, fmt);
	vsprintf( mesg_stack[0].str, fmt, ap );
	va_end(ap);

	mesg_stack[0].t = SDL_GetTicks() + mesg_timeout;
}

/*
 * renders the player
 */
void player_render (void)
{
	int i, j;
	char str[10];
	Pilot* p;
	Vector2d v;
	glColour* c;

	/* renders the player target graphics */
	if (player_target != PLAYER_ID) {
		p = pilot_get(player_target);

		if (pilot_isDisabled(p)) c = &cInert;
		else if (pilot_isFlag(p,PILOT_HOSTILE)) c = &cHostile;
		else c = &cNeutral;

		vect_csetmin( &v, VX(p->solid->pos) - p->ship->gfx_space->sw * PILOT_SIZE_APROX/2.,
				VY(p->solid->pos) + p->ship->gfx_space->sh * PILOT_SIZE_APROX/2. );
		gl_blitSprite( gui.gfx_targetPilot, &v, 0, 0, c ); /* top left */
		VX(v) += p->ship->gfx_space->sw * PILOT_SIZE_APROX;
		gl_blitSprite( gui.gfx_targetPilot, &v, 1, 0, c ); /* top right */
		VY(v) -= p->ship->gfx_space->sh * PILOT_SIZE_APROX;
		gl_blitSprite( gui.gfx_targetPilot, &v, 1, 1, c ); /* bottom right */
		VX(v) -= p->ship->gfx_space->sw * PILOT_SIZE_APROX;
		gl_blitSprite( gui.gfx_targetPilot, &v, 0, 1, c ); /* bottom left */
	}

	/* render the player */
	pilot_render(player);

	/*
	 *    G U I
	 */
	/*
	 * frame
	 */
	gl_blitStatic( gui.gfx_frame, &gui.pos_frame, NULL );

	/*
	 * radar
	 */
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	if (gui.radar.shape==RADAR_RECT)
		glTranslated( VX(gui.pos_radar) - gl_screen.w/2. + gui.radar.w/2.,
				VY(gui.pos_radar) - gl_screen.h/2. - gui.radar.h/2., 0.);
	else if (gui.radar.shape==RADAR_CIRCLE)
		glTranslated( VX(gui.pos_radar) - gl_screen.w/2.,
				VY(gui.pos_radar) - gl_screen.h/2., 0.);

	/*
	 * planets
	 */
	COLOUR(cFriend);
	planets_minimap(gui.radar.res, gui.radar.w, gui.radar.h, gui.radar.shape);

	/*
	 * weapons
	 */
	glBegin(GL_POINTS);
		COLOUR(cRadar_weap);
		weapon_minimap(gui.radar.res, gui.radar.w, gui.radar.h, gui.radar.shape);
	glEnd(); /* GL_POINTS */


	/* render the pilots */
	for (j=0, i=1; i<pilots; i++) { /* skip the player */
		if (pilot_stack[i]->id == player_target) j = i;
		else gui_renderPilot(pilot_stack[i]);
	}
	/* render the targetted pilot */
	if (j!=0) gui_renderPilot(pilot_stack[j]);


	glBegin(GL_POINTS); /* for the player */
		/* player - drawn last*/
		COLOUR(cRadar_player);
		glVertex2d(  0.,  2. ); /* we represent the player with a small + */
		glVertex2d(  0.,  1. ); 
		glVertex2d(  0.,  0. );
		glVertex2d(  0., -1. );                                             
		glVertex2d(  0., -2. );
		glVertex2d(  2.,  0. );
		glVertex2d(  1.,  0. );
		glVertex2d( -1.,  0. );
		glVertex2d( -2.,  0. );
	glEnd(); /* GL_POINTS */

	glPopMatrix(); /* GL_PROJECTION */


	/*
	 * NAV 
	 */
	if (planet_target != -1) {
	}
	else {
		i = gl_printWidth( NULL, "NAV" );
		vect_csetmin( &v, VX(gui.pos_nav) + (gui.nav.w - i)/2. ,
				VY(gui.pos_nav) - 5);
		gl_print( NULL, &v, &cConsole, "NAV" );
		i = gl_printWidth( &gui.smallFont, "No Target" );
		vect_csetmin( &v, VX(gui.pos_nav) + (gui.nav.w - i)/2. ,
				VY(gui.pos_nav) - 10 - gui.smallFont.h );
		gl_print( &gui.smallFont, &v, &cGrey, "No Target" );
	}


	/*
	 * health
	 */
	gui_renderBar( &cShield, &gui.pos_shield, &gui.shield,
			player->shield / player->shield_max );
	gui_renderBar( &cArmour, &gui.pos_armour, &gui.armour,
			player->armour / player->armour_max );
	gui_renderBar( &cEnergy, &gui.pos_energy, &gui.energy,
			player->energy / player->energy_max );


	/* 
	 * weapon 
	 */ 
	if (player->secondary==NULL) { /* no secondary weapon */ 
		i = gl_printWidth( NULL, "Secondary" ); 
		vect_csetmin( &v, VX(gui.pos_weapon) + (gui.weapon.w - i)/2., 
				VY(gui.pos_weapon) - 5); 
		gl_print( NULL, &v, &cConsole, "Secondary" ); 
		i = gl_printWidth( &gui.smallFont, "None" ); 
		vect_csetmin( &v, VX(gui.pos_weapon) + (gui.weapon.w - i)/2., 
				VY(gui.pos_weapon) - 10 - gl_defFont.h); 
		gl_print( &gui.smallFont, &v, &cGrey, "None"); 
	}  
	else {
		if (player->ammo==NULL) {
			i = gl_printWidth( &gui.smallFont, "%s", player->secondary->outfit->name);
			vect_csetmin( &v, VX(gui.pos_weapon) + (gui.weapon.w - i)/2.,
					VY(gui.pos_weapon) - (gui.weapon.h - gui.smallFont.h)/2.);
			gl_print( &gui.smallFont, &v, &cConsole, "%s", player->secondary->outfit->name );
		}
		else {
			i = gl_printWidth( &gui.smallFont, "%s", player->ammo->outfit->name);
			vect_csetmin( &v, VX(gui.pos_weapon) + (gui.weapon.w - i)/2.,
					VY(gui.pos_weapon) - 5);
			gl_print( &gui.smallFont, &v, NULL, "%s", player->ammo->outfit->name );
			i = gl_printWidth( NULL, "%d", player->ammo->quantity );
			vect_csetmin( &v, VX(gui.pos_weapon) + (gui.weapon.w - i)/2.,
					VY(gui.pos_weapon) - 10 - gl_defFont.h);
			gl_print( NULL, &v, &cConsole, "%d", player->ammo->quantity );
		}
	} 


	/*
	 * target
	 */
	if (player_target != PLAYER_ID) {
		p = pilot_get(player_target);

		gl_blitStatic( p->ship->gfx_target, &gui.pos_target, NULL );

		/* target name */
		gl_print( NULL, &gui.pos_target_name, NULL, "%s", p->name );
		gl_print( &gui.smallFont, &gui.pos_target_faction, NULL, "%s", p->faction->name );

		/* target status */
		if (pilot_isDisabled(p)) /* pilot is disabled */
			gl_print( &gui.smallFont, &gui.pos_target_health, NULL, "Disabled" );
		else if (p->shield > p->shield_max/100.) /* on shields */
			gl_print( &gui.smallFont, &gui.pos_target_health, NULL,
					"%s: %.0f%%", "Shield", p->shield/p->shield_max*100. );
		else /* on armour */
			gl_print( &gui.smallFont, &gui.pos_target_health, NULL, 
					"%s: %.0f%%", "Armour", p->armour/p->armour_max*100. );
	}
	else { /* no target */
		i = gl_printWidth( NULL, "No Target" );
		vect_csetmin( &v, VX(gui.pos_target) + (SHIP_TARGET_W - i)/2.,
				VY(gui.pos_target) + (SHIP_TARGET_H - gl_defFont.h)/2.);
		gl_print( NULL, &v, &cGrey, "No Target" );
	}


	/*
	 * misc
	 */
	vect_csetmin( &v, VX(gui.pos_misc) + 10,
			VY(gui.pos_misc) - 10 - gl_defFont.h );
	gl_print( NULL, &v, &cConsole, "Credits:" );
	if (credits >= 1000000)
		snprintf( str, 10, "%.2fM", (double)credits / 1000000.);
	else if (credits >= 1000)
		snprintf( str, 10, "%.2fK", (double)credits / 1000.);
	else snprintf (str, 10, "%d", credits );
	i = gl_printWidth( &gui.smallFont, "%s", str );
	vect_csetmin( &v, VX(gui.pos_misc) + gui.misc.w - 10 - i,
			VY(gui.pos_misc) - 10 - gl_defFont.h );
	gl_print( &gui.smallFont, &v, NULL, "%s", str );



	/*
	 * messages
	 */
	vect_csetmin( &v, VX(gui.pos_mesg),
			VY(gui.pos_mesg) + (double)(gl_defFont.h*mesg_max)*1.2 );
	for (i=0; i<mesg_max; i++) {
		VY(v) -= (double)gl_defFont.h*1.2;
		if (mesg_stack[mesg_max-i-1].str[0]!='\0') {
			if (mesg_stack[mesg_max-i-1].t < SDL_GetTicks())
				mesg_stack[mesg_max-i-1].str[0] = '\0';
			else gl_print( NULL, &v, NULL, "%s", mesg_stack[mesg_max-i-1].str );
		}
	}
}

/*
 * renders a pilot
 */
static void gui_renderPilot( const Pilot* p )
{
	int x, y, sx, sy;
	double w, h;

	x = (p->solid->pos.x - player->solid->pos.x) / gui.radar.res;
	y = (p->solid->pos.y - player->solid->pos.y) / gui.radar.res;
	sx = PILOT_SIZE_APROX/2. * p->ship->gfx_space->sw / gui.radar.res;
	sy = PILOT_SIZE_APROX/2. * p->ship->gfx_space->sh / gui.radar.res;
	if (sx < 1.) sx = 1.;
	if (sy < 1.) sy = 1.;

	if ( ((gui.radar.shape==RADAR_RECT) &&
				((ABS(x) > gui.radar.w/2+sx) || (ABS(y) > gui.radar.h/2.+sy)) ) ||
			((gui.radar.shape==RADAR_CIRCLE) &&
				((x*x+y*y) > (int)(gui.radar.w*gui.radar.w))) )
		return; /* pilot not in range */

	if (gui.radar.shape==RADAR_RECT) {
		w = gui.radar.w/2.;
		h = gui.radar.h/2.;
	}
	else if (gui.radar.shape==RADAR_CIRCLE) {
		w = gui.radar.w;
		h = gui.radar.w;
	}

	glBegin(GL_QUADS);
		/* colors */
		if (p->id == player_target) COLOUR(cRadar_targ);
		else if (pilot_isDisabled(p)) COLOUR(cInert);
		else if (pilot_isFlag(p,PILOT_HOSTILE)) COLOUR(cHostile);
		else COLOUR(cNeutral);

		/* image */
		glVertex2d( MAX(x-sx,-w), MIN(y+sy, h) ); /* top-left */
		glVertex2d( MIN(x+sx, w), MIN(y+sy, h) );/* top-right */
		glVertex2d( MIN(x+sx, w), MAX(y-sy,-h) );/* bottom-right */
		glVertex2d( MAX(x-sx,-w), MAX(y-sy,-h) );/* bottom-left */
	glEnd(); /* GL_QUADS */
}


/*
 * renders a bar (health)
 */
static void gui_renderBar( const glColour* c, const Vector2d* p,
		const Rect* r, const double w )
{
	int x, y, sx, sy;

	glBegin(GL_QUADS); /* shield */
		COLOUR(*c); 
		x = VX(*p) - gl_screen.w/2.;
		y = VY(*p) - gl_screen.h/2.;
		sx = w * r->w;
		sy = r->h;
		glVertex2d( x, y );
		glVertex2d( x + sx, y );
		glVertex2d( x + sx, y - sy );
		glVertex2d( x, y - sy );                                            
	glEnd(); /* GL_QUADS */

}


/*
 * initializes the GUI
 */
int gui_init (void)
{
	/*
	 * set gfx to NULL
	 */
	gui.gfx_frame = NULL;
	gui.gfx_targetPilot = NULL;
	gui.gfx_targetPlanet = NULL;

	/*
	 * font
	 */
	gl_fontInit( &gui.smallFont, NULL, 10 );

	/*
	 * radar
	 */
	gui.radar.res = RADAR_RES_DEFAULT;

	/*
	 * messages
	 */
   vect_csetmin( &gui.pos_mesg, 20, 30 );
   mesg_stack = calloc(mesg_max, sizeof(Mesg));

	return 0;
}


/*
 * attempts to load the actual gui
 */
int gui_load (const char* name)
{
	uint32_t bufsize;
	char *buf = pack_readfile( DATA, GUI_DATA, &bufsize );
	char *tmp;
	int found = 0;

	xmlNodePtr node;
	xmlDocPtr doc = xmlParseMemory( buf, bufsize );

	node = doc->xmlChildrenNode;
	if (!xml_isNode(node,XML_GUI_ID)) {
		ERR("Malformed '"GUI_DATA"' file: missing root element '"XML_GUI_ID"'");
		return -1;
	}

	node = node->xmlChildrenNode; /* first system node */
	if (node == NULL) {
		ERR("Malformed '"GUI_DATA"' file: does not contain elements");
		return -1;
	}                                                                                       
	do {
		if (xml_isNode(node, XML_GUI_TAG)) {

			tmp = xml_nodeProp(node,"name"); /* mallocs */

			/* is the gui we are looking for? */
			if (strcmp(tmp,name)==0) {
				found = 1;

				/* parse the xml node */
				if (gui_parse(node,name)) WARN("Trouble loading GUI '%s'", name);
				free(tmp);
				break;
			}

			free(tmp);
		}
	} while ((node = node->next));

	xmlFreeDoc(doc);
	free(buf);
	xmlCleanupParser();

	if (!found) {
		WARN("GUI '%s' not found in '"GUI_DATA"'",name);
		return -1;
	}

	return 0;
}


/*
 * used to pull out a rect from an xml node (<x><y><w><h>)
 */
static void rect_parse( const xmlNodePtr parent,
		double *x, double *y, double *w, double *h )
{
	xmlNodePtr cur;
	int param;

	param = 0;

	cur = parent->children;
	do {
		if (xml_isNode(cur,"x")) {
			if (x!=NULL) {
				*x = xml_getFloat(cur);
				param |= (1<<0);
			}
			else WARN("Extra parameter 'x' found for GUI node '%s'", parent->name);
		}
		else if (xml_isNode(cur,"y")) {
			if (y!=NULL) {
				*y = xml_getFloat(cur);
				param |= (1<<1);
			}
			else WARN("Extra parameter 'y' found for GUI node '%s'", parent->name);
		}
		else if (xml_isNode(cur,"w")) {
			if (w!=NULL) {
				*w = xml_getFloat(cur);
				param |= (1<<2);
			}
			else WARN("Extra parameter 'w' found for GUI node '%s'", parent->name);
		}
		else if (xml_isNode(cur,"h")) {
			if (h!=NULL) {
				*h = xml_getFloat(cur);
				param |= (1<<3);
			}
			else WARN("Extra parameter 'h' found for GUI node '%s'", parent->name);
		}
	} while ((cur = cur->next));

	/* check to see if we got everything we asked for */
	if (x && !(param & (1<<0)))
		WARN("Missing parameter 'x' for GUI node '%s'", parent->name);
	else if (y && !(param & (1<<1))) 
		WARN("Missing parameter 'y' for GUI node '%s'", parent->name);
	else if (w && !(param & (1<<2))) 
		WARN("Missing parameter 'w' for GUI node '%s'", parent->name);
	else if (h && !(param & (1<<3))) 
		WARN("Missing parameter 'h' for GUI node '%s'", parent->name);
}


/*
 * parse a gui node
 */
static int gui_parse( const xmlNodePtr parent, const char *name )
{
	xmlNodePtr cur, node;
	double x, y;
	char *tmp, *tmp2;


	/*
	 * gfx
	 */
	/* set as a property and not a node because it must be loaded first */
	tmp2 = xml_nodeProp(parent,"gfx");
	if (tmp2==NULL) {
		ERR("GUI '%s' has no gfx property",name);
		return -1;
	}

	/* load gfx */
	tmp = malloc( (strlen(tmp2)+strlen(GUI_GFX)+12) * sizeof(char) );
	/* frame */
	snprintf( tmp, strlen(tmp2)+strlen(GUI_GFX)+5, GUI_GFX"%s.png", tmp2 );
	if (gui.gfx_frame) gl_freeTexture(gui.gfx_frame); /* free if needed */
	gui.gfx_frame = gl_newImage( tmp );
	/* pilot */
	snprintf( tmp, strlen(tmp2)+strlen(GUI_GFX)+11, GUI_GFX"%s_pilot.png", tmp2 );
	if (gui.gfx_targetPilot) gl_freeTexture(gui.gfx_targetPilot); /* free if needed */
	gui.gfx_targetPilot = gl_newSprite( tmp, 2, 2 );
	/* planet */
	snprintf( tmp, strlen(tmp2)+strlen(GUI_GFX)+12, GUI_GFX"%s_planet.png", tmp2 );
	if (gui.gfx_targetPlanet) gl_freeTexture(gui.gfx_targetPlanet); /* free if needed */
	gui.gfx_targetPlanet = gl_newSprite( tmp, 2, 2 );
	free(tmp);
	free(tmp2);

	/*
	 * frame (based on gfx)
	 */
	vect_csetmin( &gui.pos_frame,
			gl_screen.w - gui.gfx_frame->w,     /* x */
			gl_screen.h - gui.gfx_frame->h );   /* y */

	/* now actually parse the data */
	node = parent->children;
	do { /* load all the data */

		/*
		 * offset
		 */
		if (xml_isNode(node,"offset")) {
			rect_parse( node, &x, &y, NULL, NULL );
			gui_xoff = x;
			gui_yoff = y;
		}

		/*
		 * radar
		 */
		else if (xml_isNode(node,"radar")) {

			tmp = xml_nodeProp(node,"type");

			/* make sure type is valid */
			if (strcmp(tmp,"rectangle")==0) gui.radar.shape = RADAR_RECT;
			else if (strcmp(tmp,"circle")==0) gui.radar.shape = RADAR_CIRCLE;
			else {
				WARN("Radar for GUI '%s' is missing 'type' tag or has invalid 'type' tag",name);
				gui.radar.shape = RADAR_RECT;
			}

			free(tmp);
		
			/* load the appropriate measurements */
			if (gui.radar.shape == RADAR_RECT)
				rect_parse( node, &x, &y, &gui.radar.w, &gui.radar.h );
			else if (gui.radar.shape == RADAR_CIRCLE)
				rect_parse( node, &x, &y, &gui.radar.w, NULL );
			vect_csetmin( &gui.pos_radar,
					VX(gui.pos_frame) + x,
					VY(gui.pos_frame) + gui.gfx_frame->h - y );
		}

		/*
		 * nav computer
		 */
		else if (xml_isNode(node,"nav")) {
			rect_parse( node, &x, &y, &gui.nav.w, &gui.nav.h );
			vect_csetmin( &gui.pos_nav,
					VX(gui.pos_frame) + x,
					VY(gui.pos_frame) + gui.gfx_frame->h - y - gl_defFont.h);
		}

		/*
		 * health bars
		 */
		else if (xml_isNode(node,"health")) {
			cur = node->children;
			do {
				if (xml_isNode(cur,"shield")) {
					rect_parse( cur, &x, &y, &gui.shield.w, &gui.shield.h );
					vect_csetmin( &gui.pos_shield,
						VX(gui.pos_frame) + x,
						VY(gui.pos_frame) + gui.gfx_frame->h - y );
				}
				if (xml_isNode(cur,"armour")) {
					rect_parse( cur, &x, &y, &gui.armour.w, &gui.armour.h );
					vect_csetmin( &gui.pos_armour,              
							VX(gui.pos_frame) + x,                   
							VY(gui.pos_frame) + gui.gfx_frame->h - y );
				}
				if (xml_isNode(cur,"energy")) {
					rect_parse( cur, &x, &y, &gui.energy.w, &gui.energy.h );
					vect_csetmin( &gui.pos_energy,              
							VX(gui.pos_frame) + x,                   
							VY(gui.pos_frame) + gui.gfx_frame->h - y );
				}
			} while ((cur = cur->next));
		}

		/*
		 * secondary weapon
		 */
		else if (xml_isNode(node,"weapon")) {
			rect_parse( node, &x, &y, &gui.weapon.w, &gui.weapon.h );
			vect_csetmin( &gui.pos_weapon,
					VX(gui.pos_frame) + x,
					VY(gui.pos_frame) + gui.gfx_frame->h - y - gl_defFont.h);
		}

		/*
		 * target
		 */
		else if (xml_isNode(node,"target")) {
			cur = node->children;
			do {
				if (xml_isNode(cur,"gfx")) {
					rect_parse( cur, &x, &y, NULL, NULL );
					vect_csetmin( &gui.pos_target,
							VX(gui.pos_frame) + x,
							VY(gui.pos_frame) + gui.gfx_frame->h - y - SHIP_TARGET_H );
				}
				else if (xml_isNode(cur,"name")) {
					rect_parse( cur, &x, &y, NULL, NULL );
					vect_csetmin( &gui.pos_target_name,
							VX(gui.pos_frame) + x,
							VY(gui.pos_frame) + gui.gfx_frame->h - y - gl_defFont.h);
				}
				else if (xml_isNode(cur,"faction")) {
					rect_parse( cur, &x, &y, NULL, NULL );
					vect_csetmin( &gui.pos_target_faction,
							VX(gui.pos_frame) + x,
							VY(gui.pos_frame) + gui.gfx_frame->h - y - gui.smallFont.h );
				}
				else if (xml_isNode(cur,"health")) {
					rect_parse( cur, &x, &y, NULL, NULL );
					vect_csetmin( &gui.pos_target_health,
							VX(gui.pos_frame) + x,
							VY(gui.pos_frame) + gui.gfx_frame->h - y - gui.smallFont.h );
				}
			} while ((cur = cur->next));
		}

		/*
		 * misc
		 */
		else if (xml_isNode(node,"misc")) {
			rect_parse( node, &x, &y, &gui.misc.w, &gui.misc.h );
			vect_csetmin( &gui.pos_misc,
					VX(gui.pos_frame) + x,
					VY(gui.pos_frame) + gui.gfx_frame->h - y );
		}
	} while ((node = node->next));

	return 0;
}
/*
 * frees the GUI
 */
void gui_free (void)
{
	gl_freeFont( &gui.smallFont );

	gl_freeTexture( gui.gfx_frame );
	gl_freeTexture( gui.gfx_targetPilot );
	gl_freeTexture( gui.gfx_targetPlanet );

	free(mesg_stack);
}


/*
 * used in pilot.c
 *
 * basically uses keyboard input instead of AI input
 */
void player_think( Pilot* player )
{
	if (player_isFlag(PLAYER_FACE) && (player_target != PLAYER_ID)) {
		double diff = angle_diff(player->solid->dir,
				vect_angle(&player->solid->pos, &pilot_get(player_target)->solid->pos));
		player_turn = -10.*diff;
		if (player_turn > 1.) player_turn = 1.;
		else if (player_turn < -1.) player_turn = -1.;
	}

	player->solid->dir_vel = 0.;
	if (player_turn)
		player->solid->dir_vel -= player->ship->turn * player_turn;

	if (player_isFlag(PLAYER_PRIMARY)) pilot_shoot(player,0,0);
	if (player_isFlag(PLAYER_SECONDARY) && (player_target != PLAYER_ID)) /* needs target */
		pilot_shoot(player,player_target,1);

	vect_pset( &player->solid->force, player->ship->thrust * player_acc, player->solid->dir );
}


/*
 * attempt to board the player's target
 */
void player_board (void)
{
	Pilot *p;
	
	if (player_target==PLAYER_ID) {
		player_message("You need a target to board first!");
		return;
	}

	p = pilot_get(player_target);

	if (!pilot_isDisabled(p)) {
		player_message("You cannot board a ship that isn't disabled!");
		return;
	} if (vect_dist(&player->solid->pos,&p->solid->pos) > 
			p->ship->gfx_space->sw * PILOT_SIZE_APROX) {
		player_message("You are too far away to board your target");
		return;
	} if ((pow2(VX(player->solid->vel)-VX(p->solid->vel)) +
				pow2(VY(player->solid->vel)-VY(p->solid->vel))) >
			(double)pow2(MAX_HYPERSPACE_VEL)) {
		player_message("You are going to fast to board the ship");
		return;
	}

	player_message("Ship boarding not implemented yet");
}


/*
 * get the next secondary weapon
 */
static void player_secondaryNext (void)
{
	int i = 0;
	
	/* get current secondary weapon pos */
	if (player->secondary != NULL)	
		for (i=0; i<player->noutfits; i++)
			if (&player->outfits[i] == player->secondary) {
				i++;
				break;
			}

	/* get next secondary weapon */
	for (; i<player->noutfits; i++)
		if (outfit_isProp(player->outfits[i].outfit, OUTFIT_PROP_WEAP_SECONDARY)) {
			player->secondary = player->outfits + i;
			break;
		}

	/* found no bugger outfit */
	if (i >= player->noutfits)
		player->secondary = NULL;

	/* set ammo */
	pilot_setAmmo(player);
}


/*
 * take a screenshot
 */
static void player_screenshot (void)
{
	char filename[20];

	/* TODO not overwrite old screenshots */
	strncpy(filename,"screenshot.png",20);
	gl_screenshot(filename);
}


/*
 *
 *
 *		I N P U T
 *
 */
/*
 * sets the default input keys
 */
void input_setDefault (void)
{
	input_setKeybind( "accel", KEYBIND_KEYBOARD, SDLK_UP, 0 );
	input_setKeybind( "left", KEYBIND_KEYBOARD, SDLK_LEFT, 0 );
	input_setKeybind( "right", KEYBIND_KEYBOARD, SDLK_RIGHT, 0 );
	input_setKeybind( "primary", KEYBIND_KEYBOARD, SDLK_SPACE, 0 );
	input_setKeybind( "target", KEYBIND_KEYBOARD, SDLK_TAB, 0 );
	input_setKeybind( "target_nearest", KEYBIND_KEYBOARD, SDLK_r, 0 );
	input_setKeybind( "face", KEYBIND_KEYBOARD, SDLK_a, 0 );
	input_setKeybind( "board", KEYBIND_KEYBOARD, SDLK_b, 0 );
	input_setKeybind( "secondary", KEYBIND_KEYBOARD, SDLK_LSHIFT, 0 );
	input_setKeybind( "secondary_next", KEYBIND_KEYBOARD, SDLK_w, 0 );
	input_setKeybind( "mapzoomin", KEYBIND_KEYBOARD, SDLK_9, 0 );
	input_setKeybind( "mapzoomout", KEYBIND_KEYBOARD, SDLK_0, 0 );
	input_setKeybind( "screenshot", KEYBIND_KEYBOARD, SDLK_KP_MINUS, 0 );
}

/*
 * initialization/exit functions (does not assign keys)
 */
void input_init (void)
{
	Keybind *temp;
	int i;
	for (i=0; strcmp(keybindNames[i],"end"); i++); /* gets number of bindings */
	player_input = malloc(i*sizeof(Keybind*));

	/* creates a null keybinding for each */
	for (i=0; strcmp(keybindNames[i],"end"); i++) {
		temp = MALLOC_ONE(Keybind);
		temp->name = (char*)keybindNames[i];
		temp->type = KEYBIND_NULL;
		temp->key = 0;
		temp->reverse = 1.;
		player_input[i] = temp;
	}
}
void input_exit (void)
{
	int i;
	for (i=0; strcmp(keybindNames[i],"end"); i++)
		free(player_input[i]);
	free(player_input);
}


/*
 * binds key of type type to action keybind
 *
 * @param keybind is the name of the keybind defined above
 * @param type is the type of the keybind
 * @param key is the key to bind to
 * @param reverse is whether to reverse it or not
 */
void input_setKeybind( char *keybind, KeybindType type, int key, int reverse )
{
	int i;
	for (i=0; strcmp(keybindNames[i],"end"); i++)
		if (strcmp(keybind, player_input[i]->name)==0) {
			player_input[i]->type = type;
			player_input[i]->key = key;
			player_input[i]->reverse = (reverse) ? -1. : 1. ;
			return;
		}
	WARN("Unable to set keybinding '%s', that command doesn't exist", keybind);
}


/*
 * runs the input command
 *
 * @param keynum is the index of the player_input keybind
 * @param value is the value of the keypress (defined above)
 * @param abs is whether or not it's an absolute value (for them joystick)
 */
static void input_key( int keynum, double value, int abs )
{
	/* accelerating */
	if (strcmp(player_input[keynum]->name,"accel")==0) {
		if (abs) player_acc = value;
		else player_acc += value;
		player_acc = ABS(player_acc); /* make sure value is sane */


	/* turning left */
	} else if (strcmp(player_input[keynum]->name,"left")==0) {

		/* set flags for facing correction */
		if (value==KEY_PRESS) player_setFlag(PLAYER_TURN_LEFT);
		else if (value==KEY_RELEASE) player_rmFlag(PLAYER_TURN_LEFT);

		if (abs) player_turn = -value;
		else player_turn -= value;
		if (player_turn < -1.) player_turn = -1.; /* make sure value is sane */


	/* turning right */
	} else if (strcmp(player_input[keynum]->name,"right")==0) {

		/* set flags for facing correction */
		if (value==KEY_PRESS) player_setFlag(PLAYER_TURN_RIGHT);
		else if (value==KEY_RELEASE) player_rmFlag(PLAYER_TURN_RIGHT);

		if (abs)	player_turn = value;
		else player_turn += value;
		if (player_turn > 1.) player_turn = 1.; /* make sure value is sane */


	/* shooting primary weapon */
	} else if (strcmp(player_input[keynum]->name, "primary")==0) {
		if (value==KEY_PRESS) player_setFlag(PLAYER_PRIMARY);
		else if (value==KEY_RELEASE) player_rmFlag(PLAYER_PRIMARY);


	/* targetting */
	} else if (strcmp(player_input[keynum]->name, "target")==0) {
		if (value==KEY_PRESS) player_target = pilot_getNext(player_target);

	} else if (strcmp(player_input[keynum]->name, "target_nearest")==0) {
		if (value==KEY_PRESS) player_target = pilot_getHostile();


	/* face the target */
	} else if (strcmp(player_input[keynum]->name, "face")==0) {
		if (value==KEY_PRESS) player_setFlag(PLAYER_FACE);
		else if (value==KEY_RELEASE) {
			player_rmFlag(PLAYER_FACE);

			/* turning corrections */
			player_turn = 0;
			if (player_isFlag(PLAYER_TURN_LEFT)) player_turn -= 1;
			if (player_isFlag(PLAYER_TURN_RIGHT)) player_turn += 1;
		}


	/* board them ships */
	} else if (strcmp(player_input[keynum]->name, "board")==0) {
		if (value==KEY_PRESS) player_board();


	/* shooting secondary weapon */
	} else if (strcmp(player_input[keynum]->name, "secondary")==0) {
		if (value==KEY_PRESS) player_setFlag(PLAYER_SECONDARY);
		else if (value==KEY_RELEASE) player_rmFlag(PLAYER_SECONDARY);
	

	/* selecting secondary weapon */
	} else if (strcmp(player_input[keynum]->name, "secondary_next")==0) {
		if (value==KEY_PRESS) player_secondaryNext();


	/* zooming in */
	} else if (strcmp(player_input[keynum]->name, "mapzoomin")==0) {
		if ((value==KEY_PRESS) && (gui.radar.res < RADAR_RES_MAX))
			gui.radar.res += RADAR_RES_INTERVAL;


	/* zooming out */
	} else if (strcmp(player_input[keynum]->name, "mapzoomout")==0) {
		if ((value==KEY_PRESS) && (gui.radar.res > RADAR_RES_MIN))
			gui.radar.res -= RADAR_RES_INTERVAL;


	/* take a screenshot */
	} else if (strcmp(player_input[keynum]->name, "screenshot")==0) {
		if (value==KEY_PRESS) player_screenshot();
	}
}


/*
 * events
 */
/* prototypes */
static void input_joyaxis( const unsigned int axis, const int value );
static void input_joydown( const unsigned int button );
static void input_joyup( const unsigned int button );
static void input_keydown( SDLKey key );
static void input_keyup( SDLKey key );

/*
 * joystick
 */
/* joystick axis */
static void input_joyaxis( const unsigned int axis, const int value )
{
	int i;
	for (i=0; strcmp(keybindNames[i],"end"); i++)
		if (player_input[i]->type == KEYBIND_JAXIS && player_input[i]->key == axis) {
			input_key(i,-(player_input[i]->reverse)*(double)value/32767.,1);
			return;
		}
}
/* joystick button down */
static void input_joydown( const unsigned int button )
{
	int i;
	for (i=0; strcmp(keybindNames[i],"end"); i++)
		if (player_input[i]->type == KEYBIND_JBUTTON && player_input[i]->key == button) {
			input_key(i,KEY_PRESS,0);
			return;
		}  
}
/* joystick button up */
static void input_joyup( const unsigned int button )
{
	int i;
	for (i=0; strcmp(keybindNames[i],"end"); i++)
		if (player_input[i]->type == KEYBIND_JBUTTON && player_input[i]->key == button) {
			input_key(i,KEY_RELEASE,0);
			return;
		} 
}


/*
 * keyboard
 */
/* key down */
static void input_keydown( SDLKey key )
{
	int i;
	for (i=0; strcmp(keybindNames[i],"end"); i++)
		if (player_input[i]->type == KEYBIND_KEYBOARD && player_input[i]->key == key) {
			input_key(i,KEY_PRESS,0);
			return;
		}

	/* ESC = quit */
	SDL_Event quit;
	if (key == SDLK_ESCAPE) {
		quit.type = SDL_QUIT;
		SDL_PushEvent(&quit);
	}

}
/* key up */
static void input_keyup( SDLKey key )
{
	int i;
	for (i=0; strcmp(keybindNames[i],"end"); i++)
		if (player_input[i]->type == KEYBIND_KEYBOARD && player_input[i]->key == key) {
			input_key(i,KEY_RELEASE,0);
			return;
		}
}


/*
 * global input
 *
 * basically seperates the event types
 */
void input_handle( SDL_Event* event )
{
	switch (event->type) {
		case SDL_JOYAXISMOTION:
			input_joyaxis(event->jaxis.axis, event->jaxis.value);
			break;

		case SDL_JOYBUTTONDOWN:
			input_joydown(event->jbutton.button);
			break;

		case SDL_JOYBUTTONUP:
			input_joyup(event->jbutton.button);
			break;

		case SDL_KEYDOWN:
			input_keydown(event->key.keysym.sym);
			break;

		case SDL_KEYUP:
			input_keyup(event->key.keysym.sym);
			break;
	}
}



