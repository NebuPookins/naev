/*
 * See Licensing and Copyright notice in naev.h
 */


/**
 * @file comm.c
 *
 * @brief For communicating with planets/pilots.
 */


#include "comm.h"

#include "naev.h"
#include "log.h"
#include "toolkit.h"
#include "dialogue.h"
#include "pilot.h"
#include "rng.h"
#include "nlua.h"
#include "player.h"


#define BUTTON_WIDTH    80 /**< Button width. */
#define BUTTON_HEIGHT   30 /**< Button height. */


static Pilot *comm_pilot = NULL; /**< Pilot currently talking to. */


/*
 * Prototypes.
 */
/* Static. */
static void comm_bribe( unsigned int wid, char *unused );
static unsigned int comm_getBribeAmount (void);
static char* comm_getBribeString( char *str );
/* Extern. */
void ai_setPilot( Pilot *p );


/**
 * @brief Opens the communication dialogue with a pilot.
 *
 *    @param pilot Pilot to communicate with.
 *    @return 0 on success.
 */
int comm_open( unsigned int pilot )
{
   int x,y, w;
   glTexture *logo, *gfx_comm;
   char *name, *stand;
   unsigned int wid;
   glColour *c;

   /* Get the pilot. */
   comm_pilot = pilot_get( pilot );
   if (comm_pilot == NULL)
      return -1;
  
   /* Must not be disabled. */
   if (pilot_isFlag(comm_pilot, PILOT_DISABLED)) {
      player_message("%s does not respond.", comm_pilot->name);
   }

   /* Get graphics and text. */
   gfx_comm = comm_pilot->ship->gfx_comm;
   logo = faction_logoSmall(comm_pilot->faction);
   name = comm_pilot->name;
   /* Get standing colour / text. */
   if (pilot_isFlag(comm_pilot, PILOT_HOSTILE)) {
      stand = "Hostile";
      c = &cHostile;
   }
   else {
      stand = faction_getStandingBroad(faction_getPlayer( comm_pilot->faction ));
      c = faction_getColour( comm_pilot->faction );
   }
   w = MAX(gl_printWidth( NULL, name ), gl_printWidth( NULL, stand ));
   y = gl_defFont.h*2 + 15;
   if (logo != NULL) {
      w += logo->w;
      y = MAX( y, logo->w );
   }
   x = (gfx_comm->w - w) / 2;

   /* Create the window. */
   wid = window_create( "Communication Channel", -1, -1,
         20 + gfx_comm->w + 20 + BUTTON_WIDTH + 20, 30 + gfx_comm->h + y + 5 + 20 );

   /* Create the ship image. */
   window_addRect( wid, 20, -30, gfx_comm->w, gfx_comm->h + y + 5, "rctShip", &cGrey10, 1 );
   window_addImage( wid, 20, -30, "imgShip", gfx_comm, 0 );

   /* Faction logo. */
   if (logo != NULL) {
      window_addImage( wid, x, -30 - gfx_comm->h - 5,
            "imgFaction", logo, 0 );
      x += logo->w + 10;
      y -= (logo->w - (gl_defFont.h*2 + 15)) / 2;
   }
   
   /* Name. */
   window_addText( wid, x, -30 - gfx_comm->h - y + gl_defFont.h*2 + 10,
         gfx_comm->w - x, 20, 0, "txtName",
         NULL, &cDConsole, name );

   /* Standing. */
   window_addText( wid, x, -30 - gfx_comm->h - y + gl_defFont.h + 5,
         gfx_comm->w - x, 20, 0, "txtStanding", NULL, c, stand );

   /* Buttons. */
   window_addButton( wid, -20, 20, BUTTON_WIDTH, BUTTON_HEIGHT,
         "btnClose", "Close", window_close );
   window_addButton( wid, -20, 20 + BUTTON_HEIGHT + 20,
         BUTTON_WIDTH, BUTTON_HEIGHT, "btnGreet", "Greet", NULL );
   if (!pilot_isFlag(comm_pilot, PILOT_BRIBED) && /* Not already bribed. */
         ((faction_getPlayer( comm_pilot->faction ) < 0) || /* Hostile. */
            pilot_isFlag(comm_pilot, PILOT_HOSTILE)))
      window_addButton( wid, -20, 20 + 2*BUTTON_HEIGHT + 40,
            BUTTON_WIDTH, BUTTON_HEIGHT, "btnBribe", "Bribe", comm_bribe );
   else
      window_addButton( wid, -20, 20 + 2*BUTTON_HEIGHT + 40,
            BUTTON_WIDTH, BUTTON_HEIGHT, "btnRequest", "Request...", NULL );

   return 0;
}


/**
 * @brief Tries to bribe the pilot.
 *
 *    @param wid ID of window calling the function.
 *    @param str Unused.
 */
static void comm_bribe( unsigned int wid, char *unused )
{
   (void) unused;
   int answer;
   int price;
   char *str;
   lua_State *L;

   /* Set up for the comm_get* functions. */
   ai_setPilot( comm_pilot );

   price = comm_getBribeAmount();

   /* Unbribeable. */
   if (price == 0) {
      str = comm_getBribeString( "bribe_no" );
      if (str == NULL)
         dialogue_msg("Bribe Pilot", "\"Money won't save your hide now!\"");
      else
         dialogue_msg("Bribe Pilot", "%s", str );
      return;
   }

   /* Bribe message. */
   str = comm_getBribeString( "bribe_prompt" );
   if (str == NULL) {
      answer = dialogue_YesNo( "Bribe Pilot", "\"I'm gonna need at least %d credits to not leave you as a hunk of floating debris.\"\n\nPay %d credits?", price, price );
   }
   else
      answer = dialogue_YesNo( "Bribe Pilot", "%s\n\nPay %d credits?", str, price );

   /* Said no. */
   if (answer == 0) {
      dialogue_msg("Bribe Pilot", "You decide not to pay.");
      return;
   }

   /* Check if has the money. */
   if (player->credits < price) {
      dialogue_msg("Bribe Pilot", "You don't have enough credits for the bribery.");
   }
   else {
      player->credits -= price;
      str = comm_getBribeString( "bribe_paid" );
      if (str == NULL)
         dialogue_msg("Bribe Pilot", "\"Pleasure to do business with you.\"");
      else
         dialogue_msg("Bribe Pilot", "%s", str);

      /* Mark as bribed and don't allow bribing again. */
      pilot_setFlag( comm_pilot, PILOT_BRIBED );
      L = comm_pilot->ai->L;
      lua_getglobal(L, "mem");
      lua_pushnil(L);
      lua_setfield(L, -2, "bribe");
      lua_pop(L,1);

      /* Reopen window. */
      window_destroy( wid );
      comm_open( comm_pilot->id );
   }
}


/**
 * @brief Gets the amount the communicating pilot wants as a bribe.
 *
 * Note: It's a hack around the AI stuff, probably not too good of an idea.
 *
 *    @return Amount pilot wants.
 */
static unsigned int comm_getBribeAmount (void)
{
   lua_State *L;
   unsigned int bribe;

   /* Set up the state. */
   L = comm_pilot->ai->L;
   lua_getglobal( L, "mem" );

   /* Get the bribe amount. */
   lua_getfield( L, -1, "bribe" );
   /* If not number consider unbribeable. */
   if (!lua_isnumber(L, -1))
      bribe = 0;
   else
      bribe = (unsigned int) lua_tonumber(L, -1);
   lua_pop(L, 2);
   return bribe;
}


/**
 * @brief Gets a string from the pilot's memory.
 *
 * Valid targets are:
 *    * bribe_no: unbribe message
 *    * bribe_prompt: bribe prompt
 *    * bribe_paid: paid message
 *
 *    @param str String to get.
 *    @return String matching str.
 */
static char* comm_getBribeString( char *str )
{
   lua_State *L;
   char *ret;

   /* Get memory table. */
   L = comm_pilot->ai->L;
   lua_getglobal( L, "mem" );

   /* Get str message. */
   lua_getfield(L, -1, str );
   if (!lua_isstring(L, -1))
      ret = NULL;
   else
      ret = (char*) lua_tostring(L, -1);
   lua_pop(L, 2);

   return ret;
}

