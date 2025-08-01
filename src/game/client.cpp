#include "game.h"

namespace client
{
    bool sendplayerinfo = false, sendgameinfo = false, sendcrcinfo = false, loadedmap = false, isready = false, remote = false, demoplayback = false;
    int needsmap = 0, gettingmap = 0, lastping = 0, sessionid = 0, sessionver = 0, sessionflags = 0, lastplayerinfo = 0, mastermode = 0, needclipboard = -1, demonameid = 0, triggerid = 0;
    string connectpass = "";
    hashtable<int, const char *>demonames;

    VAR(0, debugmessages, 0, 0, 1);
    ICOMMANDV(0, getready, isready ? 1 : 0);
    ICOMMANDV(0, getmapstate, mapsaving ? 2 : (maploading || loadedmap ? 1 : 0));
    ICOMMANDV(0, gettriggerid, triggerid);

    SVAR(IDF_PERSIST, demolist, "");
    VAR(0, demoendless, 0, 0, 1);
    VAR(IDF_PERSIST, showpresence, 0, 1, 2); // 0 = never show join/leave, 1 = show only during game, 2 = show when connecting/disconnecting
    VAR(IDF_PERSIST, showpresencehostinfo, 0, 0, 1);
    VAR(IDF_PERSIST, showteamchange, 0, 1, 2); // 0 = never show, 1 = show only when switching between, 2 = show when entering match too
    VAR(IDF_PERSIST, showservervariables, 0, 0, 1); // determines if variables set by the server are printed to the console
    VAR(IDF_PERSIST, showmapvotes, 0, 2, 3); // shows map votes, 1 = only mid-game (not intermision), 2 = at all times, 3 = verbose

    VAR(IDF_PERSIST, checkpointannounce, 0, 5, 7); // 0 = never, &1 = active players, &2 = all players, &4 = all players in gauntlet
    VAR(IDF_PERSIST, checkpointannouncefilter, 0, CP_ALL, CP_ALL); // which checkpoint types to announce for
    VARF(0, checkpointspawn, 0, 1, 1, game::player1->checkpointspawn = checkpointspawn; sendplayerinfo = true);
    VAR(IDF_PERSIST, demoautoclientsave, 0, 0, 1);
    ICOMMANDV(0, demoplayback, demoplayback ? 1 : 0);

    int maxmsglen() { return G(messagelength); }

    ICOMMANDV(0, numgameplayers, game::numdynents());
    ICOMMAND(0, loopgameplayers, "re", (ident *id, uint *body),
    {
        loopstart(id, stack);
        int numdyns = game::numdynents();
        loopi(numdyns)
        {
            gameent *d = (gameent *)game::iterdynents(i);
            if(!d) continue;
            loopiter(id, stack, d->clientnum);
            execute(body);
        }
        loopend(id, stack);
    });

    int otherclients(bool self, bool nospec)
    {
        int n = self ? 1 : 0;
        loopv(game::players) if(game::players[i] && game::players[i]->actortype == A_PLAYER && (!nospec || game::players[i]->state != CS_SPECTATOR)) n++;
        return n;
    }
    ICOMMAND(0, getclientcount, "ii", (int *s, int *n), intret(otherclients(*s!=0, *n!=0)));

    int numplayers()
    {
        int n = 1; // count ourselves
        loopv(game::players) if(game::players[i] && game::players[i]->actortype < A_ENEMY) n++;
        return n;
    }

    int waiting(bool state)
    {
        if(!connected(false) || !isready || game::maptime <= 0 || (state && needsmap)) // && otherclients()
            return state && needsmap ? (gettingmap ? 3 : 2) : 1;
        return 0;
    }
    ICOMMAND(0, waiting, "i", (int *n), intret(waiting(!*n)));

    // maplist commands: see gamemode.h for related macros

    // getmaplist
    // arg1: i/gamemode
    // arg2: i/mutators
    // returns: list of valid maps including previousmaps validity (cuts previousmaps entries)
    void makemaplist(int g, int m, int c)
    {
         char *list = NULL;
         loopi(2)
         {
             maplist(list, g, m, c, mapsfilter, i!=0);
             if(list)
             {
                 result(list);
                 DELETEA(list);
                 return;
             }
         }
    }
    ICOMMAND(0, getmaplist, "iii", (int *g, int *m, int *c), makemaplist(*g, *m, *c));

    // allowmaplist
    // arg1: i/gamemode
    // arg2: i/mutators
    // returns: list of valid maps regardless of previousmaps presence
    void makeallowmaplist(int g, int m)
    {
         char *list = NULL;
         loopi(2)
         {
             allowmaplist(list, g, m);
             if(list)
             {
                 result(list);
                 DELETEA(list);
                 return;
             }
         }
    }
    ICOMMAND(0, allowmaplist, "ii", (int *g, int *m), makeallowmaplist(*g, *m));

    extern int sortvotes;
    struct mapvote
    {
        vector<gameent *> players;
        string map;
        int millis, mode, muts;

        mapvote() {}
        ~mapvote() { players.shrink(0); }

        static bool compare(const mapvote &a, const mapvote &b)
        {
            if(sortvotes)
            {
                if(a.players.length() > b.players.length()) return true;
                if(a.players.length() < b.players.length()) return false;
            }
            return a.millis < b.millis;
        }
    };
    vector<mapvote> mapvotes;

    VARF(IDF_PERSIST, sortvotes, 0, 0, 1, mapvotes.sort(mapvote::compare));
    VARF(IDF_PERSIST, cleanvotes, 0, 0, 1,
    {
        if(cleanvotes && !mapvotes.empty()) loopvrev(mapvotes) if(mapvotes[i].players.empty()) mapvotes.remove(i);
    });

    void clearvotes(gameent *d, bool msg)
    {
        int found = 0;
        loopvrev(mapvotes) if(mapvotes[i].players.find(d) >= 0)
        {
            found++;
            mapvotes[i].players.removeobj(d);
            if(cleanvotes && mapvotes[i].players.empty()) mapvotes.remove(i);
        }
        if(found)
        {
            if(!mapvotes.empty()) mapvotes.sort(mapvote::compare);
            if(msg && showmapvotes >= (d == game::player1 ? 2 : 3)) echomsg(colourwhite, "%s cleared their previous vote", game::colourname(d));
        }
    }

    void vote(gameent *d, const char *text, int mode, int muts, bool force = false)
    {
        mapvote *m = NULL;
        if(!text || !*text) text = "<random>";
        if(!mapvotes.empty()) loopvrev(mapvotes)
        {
            if(!force && mapvotes[i].players.find(d) >= 0)
            {
                if(!strcmp(text, mapvotes[i].map) && mode == mapvotes[i].mode && muts == mapvotes[i].muts) return;
                mapvotes[i].players.removeobj(d);
                if(cleanvotes && mapvotes[i].players.empty()) mapvotes.remove(i);
            }
            if(!strcmp(text, mapvotes[i].map) && mode == mapvotes[i].mode && muts == mapvotes[i].muts) m = &mapvotes[i];
        }
        if(!m)
        {
            m = &mapvotes.add();
            copystring(m->map, text);
            m->mode = mode;
            m->muts = muts;
            m->millis = totalmillis ? totalmillis : 1;
        }
        m->players.add(d);
        mapvotes.sort(mapvote::compare);
        if(showmapvotes >= (!gs_playing(game::gamestate) ? 2 : 1) && !isignored(d->clientnum))
            echomsg(colourwhite, "%s suggests: \fs\fy%s\fS on \fs\fo%s\fS", game::colourname(d), server::gamename(mode, muts), mapctitle(m->map));
    }
    ICOMMAND(0, fakevote, "", (), loopi(20) vote(game::player1, "maps/bloodlust", G_DEATHMATCH, 0, true); loopi(20) vote(game::player1, "maps/dutility", G_CAPTURE, 1<<G_M_GSP1, true));

    void getvotes(int vote, int prop, int idx)
    {
        if(vote < 0) intret(mapvotes.length());
        else if(mapvotes.inrange(vote))
        {
            mapvote &v = mapvotes[vote];
            if(prop < 0) intret(4);
            else switch(prop)
            {
                case 0:
                    if(idx < 0) intret(v.players.length());
                    else if(v.players.inrange(idx)) intret(v.players[idx]->clientnum);
                    break;
                case 1: intret(v.mode); break;
                case 2: intret(v.muts); break;
                case 3: result(v.map); break;
            }
        }
    }
    ICOMMAND(0, getvote, "bbb", (int *vote, int *prop, int *idx), getvotes(*vote, *prop, *idx));

    ICOMMAND(0, loopvotes, "ree", (ident *id, uint *body, uint *none),
    {
        loopstart(id, stack);
        if(mapvotes.empty())
        {
            loopiter(id, stack, -1);
            execute(none);
        }
        else loopv(mapvotes)
        {
            loopiter(id, stack, i);
            execute(body);
        }
        loopend(id, stack);
    });

    struct demoinfo
    {
        demoheader hdr;
        string file;

        static int compare(demoinfo &a, demoinfo &b)
        {
            return naturalsort(a.file, b.file) <= 0;
        }
    };
    vector<demoinfo> demoinfos;
    vector<char *> faildemos;

    int scandemo(const char *name)
    {
        if(!name || !*name) return -1;
        loopv(demoinfos) if(!strcmp(demoinfos[i].file, name)) return i;
        loopv(faildemos) if(!strcmp(faildemos[i], name)) return -1;
        stream *f = opengzfile(name, "rb");
        if(!f)
        {
            faildemos.add(newstring(name));
            return -1;
        }
        int num = demoinfos.length();
        demoinfo &d = demoinfos.add();
        copystring(d.file, name);
        stringz(msg);
        if(f->read(&d.hdr, sizeof(demoheader)) != sizeof(demoheader) || memcmp(d.hdr.magic, VERSION_DEMOMAGIC, sizeof(d.hdr.magic)))
            formatstring(msg, "Sorry, \fs\fc%s\fS is not a demo file", name);
        else
        {
            lilswap(&d.hdr.gamever, 4);
            if(d.hdr.gamever != VERSION_GAME)
                formatstring(msg, "Demo \fs\fc%s\fS requires \fs\fc%s\fS version of %s (with protocol version %d)", name, d.hdr.gamever<VERSION_GAME ? "an older" : "a newer", VERSION_NAME, d.hdr.gamever);
        }
        delete f;
        if(msg[0])
        {
            conoutf(colourred, "%s", msg);
            demoinfos.pop();
            faildemos.add(newstring(name));
            return -1;
        }
        return num;
    }
    ICOMMAND(0, demoscan, "s", (char *name), intret(scandemo(name)));

    void resetdemos(bool all)
    {
        if(all) loopvrev(demoinfos) demoinfos.remove(i);
        loopvrev(faildemos)
        {
            DELETEA(faildemos[i]);
            faildemos.remove(i);
        }
    }
    ICOMMAND(0, demoreset, "i", (int *all), resetdemos(*all!=0));
    ICOMMAND(0, demosort, "", (), demoinfos.sort(demoinfo::compare));

    void infodemo(int idx, int prop)
    {
        if(idx < 0) intret(demoinfos.length());
        else if(demoinfos.inrange(idx))
        {
            demoinfo &d = demoinfos[idx];
            switch(prop)
            {
                case 0: intret(d.hdr.gamever); break;
                case 1: result(d.hdr.mapname); break;
                case 2: intret(d.hdr.gamemode); break;
                case 3: intret(d.hdr.mutators); break;
                case 4: intret(d.hdr.starttime); break;
                case 5: result(d.file); break;
                default: break;
            }
        }
    }
    ICOMMAND(0, demoinfo, "bb", (int *idx, int *prop), infodemo(*idx, *prop));

    ICOMMAND(0, loopdemos, "rre", (ident *id, ident *id2, uint *body),
    {
        loopstart(id, stack);
        loopstart(id2, stack2);
        loopv(demoinfos)
        {
            loopiter(id, stack, i);
            loopiter(id2, stack2, demoinfos[i].file);
            execute(body);
        }
        loopend(id, stack);
        loopend(id2, stack2);
    });

    VAR(IDF_PERSIST, authconnect, 0, 1, 1);
    VAR(IDF_PERSIST, noauthconfig, 0, 0, 1);

    string accountname = "", accountpass = "";
    ICOMMAND(0, accountname, "s", (char *s), copystring(accountname, s && *s ? s : ""));
    ICOMMAND(0, accountpass, "s", (char *s), copystring(accountpass, s && *s ? s : ""));
    ICOMMAND(0, authkey, "ss", (char *name, char *key),
    {
        copystring(accountname, name && *name ? name : "");
        copystring(accountpass, key && *key ? key : "");
    });
    ICOMMAND(0, hasauthkey, "i", (int *n), intret(accountname[0] && accountpass[0] && (!*n || authconnect) ? 1 : 0));

    void rehash()
    {
        if(noauthconfig) return;
        execfile("auth.cfg", false);
    }

    void writecfg()
    {
        if(noauthconfig || !*accountname || !*accountpass) return;
        stream *f = openutf8file("auth.cfg", "w");
        if(!f) return;
        f->printf("authkey %s %s\n", accountname, accountpass);
        delete f;
    }

    void writegamevars(const char *name, bool all = false, bool server = false)
    {
        string fname;
        if(name && *name) copystring(fname, name);
        else formatstring(fname, "vars_%s.cfg", *filetimeformat ? gettime(filetimelocal ? currenttime : clocktime, filetimeformat) : "last");

        stream *f = openfile(fname, "w");
        if(!f) return;
        vector<ident *> ids;
        enumerate(idents, ident, id, ids.add(&id));
        ids.sortname();
        loopv(ids)
        {
            ident &id = *ids[i];
            if(id.flags&IDF_CLIENT && !(id.flags&IDF_READONLY) && !(id.flags&IDF_MAP)) switch(id.type)
            {
                case ID_VAR:
                    if(*id.storage.i == id.def.i)
                    {
                        if(all) f->printf("// ");
                        else break;
                    }
                    if(server) f->printf("sv_");
                    f->printf("%s %s\n", escapeid(id), intstr(&id));
                    break;
                case ID_FVAR:
                    if(*id.storage.f == id.def.f)
                    {
                        if(all) f->printf("// ");
                        else break;
                    }
                    if(server) f->printf("sv_");
                    f->printf("%s %s\n", escapeid(id), floatstr(*id.storage.f));
                    break;
                case ID_SVAR:
                    if(!strcmp(*id.storage.s, id.def.s))
                    {
                        if(all) f->printf("// ");
                        else break;
                    }
                    if(server) f->printf("sv_");
                    f->printf("%s %s\n", escapeid(id), escapestring(*id.storage.s));
                    break;
            }
        }
        delete f;
    }
    ICOMMAND(0, writevars, "sii", (char *name, int *all, int *sv), if(!(identflags&IDF_MAP)) writegamevars(name, *all!=0, *sv!=0));

    void writegamevarsinfo(const char *name)
    {
        if(!name || !*name) name = "varsinfo.txt";
        stream *f = openfile(name, "w");
        if(!f) return;
        f->printf("// List of vars properties, fields are separated by tabs; empty if nonexistent\n");
        f->printf("// Fields: NAME TYPE FLAGS ARGS VALTYPE VALUE MIN MAX DESC USAGE\n");
        vector<ident *> ids;
        enumerate(idents, ident, id, ids.add(&id));
        ids.sortname();
        loopv(ids)
        {
            ident &id = *ids[i];
            if(!(id.flags&IDF_SERVER)) // Exclude sv_* duplicates
            {
                f->printf("%s\t%d\t%d", id.name, id.type, id.flags);
                switch(id.type)
                {
                    case ID_VAR:
                        f->printf("\t\t"); // empty ARGS VALTYPE
                        if(!(id.flags&IDF_HEX))
                            f->printf("\t%d\t%d\t%d", id.def.i, id.minval, id.maxval);
                        else
                        {
                            if(id.maxval == 0xFFFFFF)
                                f->printf("\t0x%.6X\t0x%.6X\t0x%.6X", id.def.i, id.minval, id.maxval);
                            else if(uint(id.maxval) == 0xFFFFFFFFU)
                                f->printf("\t0x%.8X\t0x%.8X\t0x%.8X", uint(id.def.i), uint(id.minval), uint(id.maxval));
                            else
                                f->printf("\t0x%X\t0x%X\t0x%X", id.def.i, id.minval, id.maxval);
                        }
                        break;
                    case ID_FVAR:
                        f->printf("\t\t\t%s\t%s\t%s", floatstr(id.def.f), floatstr(id.minvalf), floatstr(id.maxvalf)); // empty ARGS VALTYPE
                        break;
                    case ID_SVAR:
                        f->printf("\t\t\t%s\t\t", escapestring(id.def.s)); // empty ARGS VALTYPE MIN MAX
                        break;
                    case ID_ALIAS:
                        f->printf("\t\t%d", id.valtype); // empty ARGS
                        switch(id.valtype)
                        {
                            case VAL_NULL:
                                f->printf("\tNULL");
                                break;
                            case VAL_INT:
                                f->printf("\t%d", id.val.i);
                                break;
                            case VAL_FLOAT:
                                f->printf("\t%s", floatstr(id.val.f));
                                break;
                            case VAL_STR:
                                f->printf("\t%s", escapestring(id.val.s));
                                break;
                            case VAL_CSTR:
                                f->printf("\t%s", escapestring(id.val.cstr));
                                break;
                        }
                        f->printf("\t\t"); // empty MIN MAX
                        break;
                    case ID_COMMAND:
                        f->printf("\t%s\t\t\t\t", escapestring(id.args)); // empty VALTYPE VALUE MIN MAX
                        break;
                }
                // empty if nonexistent
                f->printf("\t%s", escapestring(id.desc ? id.desc : ""));
                stringz(fields);
                loopvj(id.fields) concformatstring(fields, "%s%s", j ? " " : "", id.fields[j]);
                f->printf("\t%s", escapestring(*fields ? fields : ""));
                f->printf("\n");
            }
        }
        delete f;
    }
    ICOMMAND(0, writevarsinfo, "s", (char *name), if(!(identflags&IDF_MAP)) writegamevarsinfo(name));

    // collect c2s messages conveniently
    vector<uchar> messages;
    bool messagereliable = false;
    ICOMMAND(0, mastermode, "i", (int *val), addmsg(N_MASTERMODE, "ri", *val));

    VAR(IDF_PERSIST, colourchat, 0, 1, 1);
    SVAR(IDF_PERSIST, filterwords, "");

    VAR(IDF_PERSIST, showlaptimes, 0, 2, 3); // 0 = off, 1 = only player, 2 = +humans, 3 = +bots

    const char *defaultserversort()
    {
        static string vals;
        formatstring(vals, "%d %d %d", SINFO_NUMPLRS, SINFO_PRIO, SINFO_PING);
        return vals;
    }

    void resetserversort()
    {
        setsvarchecked(getident("serversort"), defaultserversort());
    }
    ICOMMAND(0, serversortreset, "", (), resetserversort());

    vector<int> serversortstyles;
    void updateserversort();
    SVARF(IDF_PERSIST, serversort, defaultserversort(),
    {
        if(!serversort[0] || serversort[0] == '[')
        {
            delete[] serversort;
            serversort = newstring(defaultserversort());
        }
        updateserversort();
    });

    void updateserversort()
    {
        vector<char *> styles;
        explodelist(serversort, styles, SINFO_MAX);
        serversortstyles.setsize(0);
        loopv(styles) serversortstyles.add(parseint(styles[i]));
        styles.deletearrays();
    }

    void getvitem(gameent *d, int n, int v)
    {
        if(n < 0) intret(d->vitems.length());
        else if(v < 0) intret(2);
        else if(d->vitems.inrange(n)) switch(v)
        {
            case 0: intret(d->vitems[n]); break;
            case 1: if(vanities.inrange(d->vitems[n])) result(vanities[d->vitems[n]].ref); break;
            default: break;
        }
    }
    ICOMMAND(0, getplayervitem, "bi", (int *n, int *v), getvitem(game::player1, *n, *v));

    int state() { return game::player1->state; }
    const char *getname() { return game::player1->name; }

    ICOMMANDV(0, playerstate, game::player1->state);
    ICOMMAND(0, getplayercolour, "bgf", (int *m, float *f, float *x), intret(game::getcolour(game::player1, *m, *f >= 0 && *f <= 10 ? *f : 1.f, *x)));
    ICOMMANDV(0, playerteam, game::player1->team);
    ICOMMANDVS(0, playerteamname, TEAM(game::player1->team, name));
    ICOMMANDVS(0, playerteamicon, hud::teamtexname(game::player1->team));
    ICOMMANDV(0, playerteamcolour, TEAM(game::player1->team, colour));
    ICOMMANDV(0, playercn, game::player1->clientnum);

    void setplayername(const char *name)
    {
        if(!name || !*name) return;
        string namestr;
        filterstring(namestr, name, true, true, true, true, MAXNAMELEN);
        if(!*namestr || !strcmp(game::player1->name, namestr)) return;
        game::player1->setname(namestr);
        if(initing == NOT_INITING) echomsg(colourmagenta, "You are now known as %s", game::player1->name);
        sendplayerinfo = true;
    }
    SVARF(IDF_PERSIST, playername, "", setplayername(playername));

    #define SETPLAYERINFO(name, access, flags, maxval) \
        void setplayer##name(int value) \
        { \
            int realval = min(value < 0 ? rnd(min(maxval, 0xFFFFFE) + 1) : value, maxval); \
            game::player1->access = realval; \
            sendplayerinfo = true; \
        } \
        VARF(IDF_PERSIST|flags, player##name, -1, rnd(min(maxval, 0xFFFFFE) + 1), maxval, setplayer##name(player##name));

    SETPLAYERINFO(colour, colours[0], IDF_HEX, 0xFFFFFF);
    SETPLAYERINFO(colour2, colours[1], IDF_HEX, 0xFFFFFF);
    SETPLAYERINFO(model, model, 0, PLAYERTYPES-1);

    SVARF(IDF_PERSIST, playervanity, "", game::player1->setvanity(playervanity); sendplayerinfo = true;);
    SVARF(IDF_PERSIST, playermixer, "", game::player1->setmixer(playermixer); sendplayerinfo = true;);

    void setloadweap(const char *list)
    {
        vector<int> items;
        if(list && *list)
        {
            vector<char *> chunk;
            explodelist(list, chunk);
            loopv(chunk)
            {
                if(!chunk[i] || !*chunk[i] || !isnumeric(*chunk[i])) continue;
                int v = parseint(chunk[i]);
                items.add(v >= W_OFFSET && v < W_ITEM ? v : 0);
            }
            chunk.deletearrays();
        }
        game::player1->loadweap.shrink(0);
        loopv(items) if(game::player1->loadweap.find(items[i]) < 0)
        {
            game::player1->loadweap.add(items[i]);
            if(game::player1->loadweap.length() >= W_LOADOUT) break;
        }
        sendplayerinfo = true;
    }
    SVARF(IDF_PERSIST, playerloadweap, "", setloadweap(playerloadweap));

    void setrandweap(const char *list)
    {
        vector<int> items;
        if(list && *list)
        {
            vector<char *> chunk;
            explodelist(list, chunk);
            loopv(chunk)
            {
                if(!chunk[i] || !*chunk[i] || !isnumeric(*chunk[i])) continue;
                int v = parseint(chunk[i]);
                items.add(v ? 1 : 0);
            }
            chunk.deletearrays();
        }
        game::player1->randweap.shrink(0);
        loopv(items)
        {
            game::player1->randweap.add(items[i]);
            if(game::player1->randweap.length() >= W_LOADOUT) break;
        }
        sendplayerinfo = true;
    }
    SVARF(IDF_PERSIST, playerrandweap, "", setrandweap(playerrandweap));

    ICOMMAND(0, getrandweap, "i", (int *n), intret(game::player1->randweap.inrange(*n) ? game::player1->randweap[*n] : 1));
    ICOMMAND(0, getloadweap, "i", (int *n), intret(game::player1->loadweap.inrange(*n) ? game::player1->loadweap[*n] : -1));
    ICOMMAND(0, allowedweap, "i", (int *n), intret(isweap(*n) && m_check(W(*n, modes), W(*n, muts), game::gamemode, game::mutators) && !W(*n, disabled) ? 1 : 0));
    ICOMMAND(0, hasloadweap, "bb", (int *g, int *m), intret(m_loadout(m_game(*g) ? *g : game::gamemode, *m >= 0 ? *m : game::mutators) ? 1 : 0));

    int teamfromname(const char *team)
    {
        if(m_team(game::gamemode, game::mutators))
        {
            if(team[0])
            {
                int t = atoi(team);
                loopi(numteams(game::gamemode, game::mutators))
                {
                    if((t && t == i+T_FIRST) || cubecaseequal(TEAM(i+T_FIRST, name), team))
                    {
                        return i+T_FIRST;
                    }
                }
            }
            return T_FIRST;
        }
        return T_NEUTRAL;
    }

    void switchteam(const char *team)
    {
        if(team[0])
        {
            if(m_team(game::gamemode, game::mutators))
            {
                int t = teamfromname(team);
                if(isteam(game::gamemode, game::mutators, t, T_FIRST)) addmsg(N_SWITCHTEAM, "ri", t);
            }
            else conoutf(colourred, "Can only change teams when actually playing in team games");
        }
        else conoutf(colourgreen, "Your team is: %s", game::colourteam(game::player1->team));
    }
    ICOMMAND(0, team, "s", (char *s), switchteam(s));

    bool allowedittoggle(bool edit)
    {
        bool allow = edit || m_edit(game::gamemode); // && game::player1->state == CS_ALIVE);
        if(!allow) conoutf(colourred, "You must start an editing game to edit the map");
        return allow;
    }

    void edittoggled(bool edit)
    {
        if(!edit && (game::maptime <= 0 || game::player1->state != CS_EDITING)) return;
        game::player1->editspawn(game::gamemode, game::mutators, game::player1->gethealth(game::gamemode, game::mutators));
        game::player1->state = edit ? CS_EDITING : (m_edit(game::gamemode) ? CS_ALIVE : CS_DEAD);
        game::player1->o = camera1->o;
        game::player1->yaw = camera1->yaw;
        game::player1->pitch = camera1->pitch;
        game::player1->resetinterp();
        game::resetstate();
        game::specreset();
        physics::entinmap(game::player1, true); // find spawn closest to current floating pos
        projs::removeplayer(game::player1);
        if(m_edit(game::gamemode)) addmsg(N_EDITMODE, "ri", edit ? 1 : 0);
    }

    ICOMMANDV(0, focusedplayer, game::focus ? game::focus->clientnum : game::player1->clientnum);

    int getcn(physent *d)
    {
        if(!d || !gameent::is(d)) return -1;
        return ((gameent *)d)->clientnum;
    }

    int parseplayer(const char *arg)
    {
        if(!arg || !*arg) return game::player1->clientnum;
        char *end;
        int n = strtol(arg, &end, 10);
        if(*arg && !*end)
        {
            if(n != game::player1->clientnum && !game::players.inrange(n)) return -1;
            return n;
        }
        #define PARSEPLAYER(op,val) \
        { \
            gameent *o = game::player1; \
            if(op(arg, val)) return o->clientnum; \
            loopv(game::players) if(game::players[i]) \
            { \
                o = game::players[i]; \
                if(op(arg, val)) return o->clientnum; \
            } \
        }
        PARSEPLAYER(!strcmp, game::colourname(o, NULL, false, true, 0));
        PARSEPLAYER(cubecaseequal, game::colourname(o, NULL, false, true, 0));
        PARSEPLAYER(!strcmp, o->name);
        PARSEPLAYER(cubecaseequal, o->name);
        #define PARSEPLAYERN(op,val) \
        { \
            gameent *o = game::player1; \
            if(op(arg, val, len)) return o->clientnum; \
            loopv(game::players) if(game::players[i]) \
            { \
                o = game::players[i]; \
                if(op(arg, val, len)) return o->clientnum; \
            } \
        }
        size_t len = strlen(arg);
        PARSEPLAYERN(!strncmp, game::colourname(o, NULL, false, true, 0));
        PARSEPLAYERN(cubecaseequal, game::colourname(o, NULL, false, true, 0));
        PARSEPLAYERN(!strncmp, o->name);
        PARSEPLAYERN(cubecaseequal, o->name);
        return -1;
    }
    ICOMMAND(IDF_NAMECOMPLETE, getclientnum, "s", (char *who), intret(parseplayer(who)));

    void listclients(bool local, int noai)
    {
        vector<char> buf;
        string cn;
        int numclients = 0;
        if(local)
        {
            formatstring(cn, "%d", game::player1->clientnum);
            buf.put(cn, strlen(cn));
            numclients++;
        }
        loopv(game::players) if(game::players[i] && (!noai || game::players[i]->actortype > (noai >= 2 ? A_PLAYER : A_BOT)))
        {
            formatstring(cn, "%d", game::players[i]->clientnum);
            if(numclients++) buf.add(' ');
            buf.put(cn, strlen(cn));
        }
        buf.add('\0');
        result(buf.getbuf());
    }
    ICOMMAND(0, listclients, "ii", (int *local, int *noai), listclients(*local!=0, *noai));

    void getlastclientnum()
    {
        int cn = game::player1->clientnum;
        loopv(game::players) if(game::players[i] && game::players[i]->clientnum > cn) cn = game::players[i]->clientnum;
        intret(cn);
    }
    ICOMMAND(0, getlastclientnum, "", (), getlastclientnum());

    VAR(0, numplayertypes, 1, PLAYERTYPES, -1);
    ICOMMAND(0, getmodelname, "ib", (int *mdl, int *idx), result(*mdl >= 0 ? playertypes[*mdl%PLAYERTYPES][*idx >= 0 ? clamp(*idx, 0, 6) : 6] : ""));

    ICOMMANDVF(0, cameraposx, camera1->o.x);
    ICOMMANDVF(0, cameraposy, camera1->o.y);
    ICOMMANDVF(0, cameraposz, camera1->o.z);
    ICOMMANDVF(0, camerayaw, camera1->yaw);
    ICOMMANDVF(0, camerapitch, camera1->pitch);
    ICOMMANDVF(0, cameraroll, camera1->roll);
    ICOMMAND(0, cameraoffyaw, "f", (float *yaw), floatret(*yaw-camera1->yaw));

    bool radarallow(const vec &o, gameent *d, vec &dir, float &dist, bool self)
    {
        if(m_hard(game::gamemode, game::mutators) || (!self && d == game::focus)) return false;
        if(d->state != CS_ALIVE && d->state != CS_EDITING && d->state != CS_DEAD && (!d->lastdeath || d->state != CS_WAITING)) return false;
        if(m_duke(game::gamemode, game::mutators) && (!d->lastdeath || lastmillis-d->lastdeath >= 1000)) return false;
        if(d->actortype >= A_ENVIRONMENT && !d->ishighlight(game::focus)) return false;
        dir = vec(d->center()).sub(o);
        dist = dir.magnitude();
        return d->ishighlight(game::focus) || !hud::radarlimited(dist);
    }

    int getresidualfx(gameent *d, int n, int c)
    {
        int rescount = 0, len = n > 0 ? n : 3000;
        bool buff = d->isalive() && d->lastbuff;
        if(buff) rescount++;
        bool critical = d->isalive() && d->health <= d->gethealth(game::gamemode, game::mutators) * game::damagecritical;
        if(critical) rescount++;
        #define RESIDUAL(name, type, pulse) if(d->name##time && d->name##func(lastmillis, d->name##time)) rescount++;
        RESIDUALS
        #undef RESIDUAL

        if(rescount > 0)
        {
            int iter = len/rescount, count = 0, ptype = -1, cur = lastmillis%len;
            if(critical)
            {
                if(cur >= count*iter) ptype = PULSE_ALERT;
                count++;
            }
            if(buff)
            {
                if(cur >= count*iter) ptype = PULSE_BUFF;
                count++;
            }
            #define RESIDUAL(name, type, pulse) \
                if(d->name##time && d->name##func(lastmillis, d->name##time)) \
                { \
                    if(cur >= count*iter) ptype = PULSE_##pulse; \
                    count++; \
                }
            RESIDUALS
            #undef RESIDUAL
            if(ptype >= 0) return pulsehexcol(d, ptype, c >= -1 ? c : 50);
        }
        return -1;
    }

    bool haspriv(gameent *d, int priv)
    {
        if(!d) return false;
        if(!priv || (d == game::player1 && !remote)) return true;
        return (d->privilege&PRIV_TYPE) >= priv;
    }
    void getclientversion(int cn, int prop)
    {
        gameent *d = cn >= 0 ? game::getclient(cn) : game::player1;
        if(d) switch(prop)
        {
            case -3:
            {
                defformatstring(branch, "%s", d->version.branch);
                if(d->version.build > 0) concformatstring(branch, "-%d", d->version.build);
                defformatstring(str, "%d.%d.%d-%s%d-%s", d->version.major, d->version.minor, d->version.patch, plat_name(d->version.platform), d->version.arch, branch);
                result(str);
            }
            case -2: result(plat_name(d->version.platform)); break;
            case -1: intret(16);
            case 0: intret(d->version.major); break;
            case 1: intret(d->version.minor); break;
            case 2: intret(d->version.patch); break;
            case 3: intret(d->version.game); break;
            case 4: intret(d->version.platform); break;
            case 5: intret(d->version.arch); break;
            case 6: intret(d->version.gpuglver); break;
            case 7: intret(d->version.gpuglslver); break;
            case 8: intret(d->version.crc); break;
            case 9: result(d->version.gpuvendor); break;
            case 10: result(d->version.gpurenderer); break;
            case 11: result(d->version.gpuversion); break;
            case 13: result(d->version.branch); break;
            case 14: intret(d->version.build); break;
            case 15: result(d->version.revision); break;
            default: break;
        }
    }
    ICOMMAND(IDF_NAMECOMPLETE, getclientversion, "si", (char *who, int *prop), getclientversion(parseplayer(who), *prop));

    bool isspectator(int cn)
    {
        gameent *d = game::getclient(cn);
        return d && d->state == CS_SPECTATOR;
    }
    ICOMMAND(IDF_NAMECOMPLETE, isspectator, "s", (char *who), intret(isspectator(parseplayer(who)) ? 1 : 0));

    bool isquarantine(int cn)
    {
        gameent *d = game::getclient(cn);
        return d && d->quarantine;
    }
    ICOMMAND(IDF_NAMECOMPLETE, isquarantine, "s", (char *who), intret(isquarantine(parseplayer(who)) ? 1 : 0));

    bool isai(int cn, int type)
    {
        gameent *d = game::getclient(cn);
        int actortype = type > 0 && type < A_MAX ? type : A_BOT;
        return d && d->actortype == actortype;
    }
    ICOMMAND(IDF_NAMECOMPLETE, isai, "si", (char *who, int *type), intret(isai(parseplayer(who), *type) ? 1 : 0));

    bool mutscmp(int req, int limit)
    {
        if(req)
        {
            if(!limit) return false;
            loopi(G_M_MAX) if(req&(1<<i) && !(limit&(1<<i))) return false;
        }
        return true;
    }

    bool ismodelocked(int reqmode, int reqmuts, int askmuts = 0, const char *reqmap = NULL)
    {
        reqmuts |= mutslockforce;
        if(!m_game(reqmode) || (m_local(reqmode) && remote)) return true;
        if(!reqmap || !*reqmap) reqmap = "<random>";
        bool israndom = !strcmp(reqmap, "<random>");
        if(G(votelock)) switch(G(votelocktype))
        {
            case 1: if(!haspriv(game::player1, G(votelock))) return true; break;
            case 2:
                if(!israndom && !m_edit(reqmode))
                {
                    int n = listincludes(previousmaps, reqmap, strlen(reqmap));
                    if(n >= 0 && n < G(maphistory) && !haspriv(game::player1, G(votelock))) return true;
                }
                break;
            case 0: default: break;
        }
        modecheck(reqmode, reqmuts, askmuts);
        if(askmuts && !mutscmp(askmuts, reqmuts)) return true;
        if(G(modelock)) switch(G(modelocktype))
        {
            case 1: if(!haspriv(game::player1, G(modelock))) return true; break;
            case 2: if((!((1<<reqmode)&G(modelockfilter)) || !mutscmp(reqmuts, G(mutslockfilter))) && !haspriv(game::player1, G(modelock))) return true; break;
            case 0: default: break;
        }
        if(!israndom && !m_edit(reqmode) && G(mapslock))
        {
            char *list = NULL;
            switch(G(mapslocktype))
            {
                case 1:
                {
                    list = newstring(G(allowmaps));
                    mapcull(list, reqmode, reqmuts, otherclients(true), G(mapsfilter), true);
                    break;
                }
                case 2:
                {
                    maplist(list, reqmode, reqmuts, otherclients(true), G(mapsfilter), true);
                    break;
                }
                case 0: default: break;
            }
            if(list)
            {
                if(listincludes(list, reqmap, strlen(reqmap)) < 0 && !haspriv(game::player1, G(modelock)))
                {
                    DELETEA(list);
                    return true;
                }
                DELETEA(list);
            }
        }
        return false;
    }
    ICOMMAND(0, ismodelocked, "iiis", (int *g, int *m, int *a, char *s), intret(ismodelocked(*g, *m, *a, s) ? 1 : 0));

    void getmastermode(int n)
    {
        switch(n)
        {
            case 0: intret(mastermode); return;
            case 1: result(mastermodename(mastermode)); return;
            default: break;
        }
        intret(-1);
    }
    ICOMMAND(0, getmastermode, "i", (int *n), getmastermode(*n));

    void addcontrol(const char *arg, int type, const char *msg)
    {
        int i = parseplayer(arg);
        if(i >= 0) addmsg(N_ADDCONTROL, "ri2s", i, type, msg);
    }
    ICOMMAND(IDF_NAMECOMPLETE, kick, "ss", (char *s, char *m), addcontrol(s, -1, m));
    ICOMMAND(IDF_NAMECOMPLETE, allow, "ss", (char *s, char *m), addcontrol(s, ipinfo::ALLOW, m));
    ICOMMAND(IDF_NAMECOMPLETE, ban, "ss", (char *s, char *m), addcontrol(s, ipinfo::BAN, m));
    ICOMMAND(IDF_NAMECOMPLETE, mute, "ss", (char *s, char *m), addcontrol(s, ipinfo::MUTE, m));
    ICOMMAND(IDF_NAMECOMPLETE, limit, "ss", (char *s, char *m), addcontrol(s, ipinfo::LIMIT, m));
    ICOMMAND(IDF_NAMECOMPLETE, except, "ss", (char *s, char *m), addcontrol(s, ipinfo::EXCEPT, m));

    ICOMMAND(0, clearallows, "", (), addmsg(N_CLRCONTROL, "ri", ipinfo::ALLOW));
    ICOMMAND(0, clearbans, "", (), addmsg(N_CLRCONTROL, "ri", ipinfo::BAN));
    ICOMMAND(0, clearmutes, "", (), addmsg(N_CLRCONTROL, "ri", ipinfo::MUTE));
    ICOMMAND(0, clearlimits, "", (), addmsg(N_CLRCONTROL, "ri", ipinfo::LIMIT));
    ICOMMAND(0, clearexcepts, "", (), addmsg(N_CLRCONTROL, "ri", ipinfo::EXCEPT));

    vector<char *> ignores;
    void ignore(int cn)
    {
        gameent *d = game::getclient(cn);
        if(!d || d == game::player1) return;
        if(!strcmp(d->hostip, "*"))
        {
            conoutf(colourred, "Cannot ignore %s: host information is private", game::colourname(d));
            return;
        }
        if(ignores.find(d->hostip) < 0)
        {
            conoutf(colouryellow, "Ignoring: \fs%s\fS (\fs\fc%s\fS)", game::colourname(d), d->hostip);
            ignores.add(d->hostip);
        }
        else
            conoutf(colourred, "Already ignoring: \fs%s\fS (\fs\fc%s\fS)", game::colourname(d), d->hostip);
    }

    void unignore(int cn)
    {
        gameent *d = game::getclient(cn);
        if(!d) return;
        if(!strcmp(d->hostip, "*"))
        {
            conoutf(colourred, "Cannot unignore %s: host information is private", game::colourname(d));
            return;
        }
        if(ignores.find(d->hostip) >= 0)
        {
            conoutf(colouryellow, "Stopped ignoring: \fs%s\fS (\fs\fc%s\fS)", game::colourname(d), d->hostip);
            ignores.removeobj(d->hostip);
        }
        else
            conoutf(colourred, "You are not ignoring: \fs%s\fS (\fs\fc%s\fS)", game::colourname(d), d->hostip);
    }

    bool isignored(int cn)
    {
        gameent *d = game::getclient(cn);
        if(!d || !strcmp(d->hostip, "*")) return false;
        return ignores.find(d->hostip) >= 0;
    }

    ICOMMAND(IDF_NAMECOMPLETE, ignore, "s", (char *arg), ignore(parseplayer(arg)));
    ICOMMAND(IDF_NAMECOMPLETE, unignore, "s", (char *arg), unignore(parseplayer(arg)));
    ICOMMAND(IDF_NAMECOMPLETE, isignored, "s", (char *arg), intret(isignored(parseplayer(arg)) ? 1 : 0));

    void setteam(const char *arg1, const char *arg2)
    {
        if(m_team(game::gamemode, game::mutators))
        {
            int i = parseplayer(arg1);
            if(i >= 0)
            {
                int t = teamfromname(arg2);
                if(t) addmsg(N_SETTEAM, "ri2", i, t);
            }
        }
        else conoutf(colourred, "Can only change teams in team games");
    }
    ICOMMAND(IDF_NAMECOMPLETE, setteam, "ss", (char *who, char *team), setteam(who, team));

    void hashpwd(const char *pwd)
    {
        if(game::player1->clientnum < 0) return;
        string hash;
        server::hashpassword(game::player1->clientnum, sessionid, pwd, hash);
        result(hash);
    }
    COMMAND(0, hashpwd, "s");

    void setpriv(const char *arg)
    {
        if(!arg[0]) return;
        int val = 1;
        stringz(hash);
        if(!arg[1] && isdigit(arg[0])) val = parseint(arg);
        else server::hashpassword(game::player1->clientnum, sessionid, arg, hash);
        addmsg(N_SETPRIV, "ris", val, hash);
    }
    COMMAND(0, setpriv, "s");

    void addpriv(int cn, int priv)
    {
        addmsg(N_ADDPRIV, "ri2", cn, priv);
    }
    ICOMMAND(IDF_NAMECOMPLETE, addpriv, "si", (char *who, int *priv), addpriv(parseplayer(who), *priv));
    ICOMMAND(IDF_NAMECOMPLETE, resetpriv, "s", (char *who), addpriv(parseplayer(who), -1));

    void tryauth()
    {
        if(accountname[0]) addmsg(N_AUTHTRY, "rs", accountname);
        else conoutf(colourred, "No account set for \fcauth");
    }
    ICOMMAND(0, auth, "", (), tryauth());

    void togglespectator(int cn, int val)
    {
        if(cn >= 0) addmsg(N_SPECTATOR, "ri2", cn, val);
    }
    ICOMMAND(IDF_NAMECOMPLETE, spectator, "si", (char *who, int *val), togglespectator(parseplayer(who), *val));
    ICOMMAND(0, spectate, "i", (int *val), togglespectator(game::player1->clientnum, *val));

    void connectattempt(const char *name, int port, const char *password, const ENetAddress &address)
    {
        if(*password) { copystring(connectpass, password); }
        else memset(connectpass, 0, sizeof(connectpass));
    }

    void connectfail()
    {
        memset(connectpass, 0, sizeof(connectpass));
    }

    void gameconnect(bool _remote)
    {
        remote = _remote;
        if(editmode) toggleedit(true);
        loopi(SURFACE_ALL) UI::hideui(NULL, i);
        game::updatemusic(10, true);
    }

    void gamedisconnect(int clean)
    {
        if(editmode) toggleedit(true);
        remote = isready = sendplayerinfo = sendgameinfo = sendcrcinfo = loadedmap = false;
        gettingmap = needsmap = sessionid = sessionver = lastplayerinfo = mastermode = triggerid = 0;
        messages.shrink(0);
        mapvotes.shrink(0);
        messagereliable = false;
        projs::removeplayer(game::player1);
        removetrackedparticles(game::player1);
        removetrackeddynlights(game::player1);
        removetrackedsounds(game::player1);
        game::player1->clientnum = -1;
        game::player1->privilege = PRIV_NONE;
        game::player1->handle[0] = game::player1->steamid[0] = '\0';
        game::gamemode = G_EDITING;
        game::mutators = game::maptime = game::timesync = game::timelast = 0;
        loopv(game::players) if(game::players[i]) game::clientdisconnected(i);
        game::waiting.setsize(0);
        emptymap(0, true, NULL, false);
        game::updatemusic(9, true);
        enumerate(idents, ident, id,
        {
            if(id.flags&IDF_CLIENT) switch(id.type)
            {
                case ID_VAR: setvar(id.name, id.def.i, true); break;
                case ID_FVAR: setfvar(id.name, id.def.f, true); break;
                case ID_SVAR: setsvar(id.name, *id.def.s ? id.def.s : "", true); break;
                default: break;
            }
        });

        triggereventcallbacks(CMD_EVENT_GAME_LEAVE);
    }

    bool addmsg(int type, const char *fmt, ...)
    {
        static uchar buf[MAXTRANS];
        ucharbuf p(buf, MAXTRANS);
        putint(p, type);
        int numi = 1, nums = 0;
        bool reliable = false;
        if(fmt)
        {
            va_list args;
            va_start(args, fmt);
            while(*fmt) switch(*fmt++)
            {
                case 'r': reliable = true; break;
                case 'v':
                {
                    int n = va_arg(args, int);
                    int *v = va_arg(args, int *);
                    loopi(n) putint(p, v[i]);
                    numi += n;
                    break;
                }
                case 'i':
                {
                    int n = isdigit(*fmt) ? *fmt++-'0' : 1;
                    loopi(n) putint(p, va_arg(args, int));
                    numi += n;
                    break;
                }
                case 'u':
                {
                    int n = isdigit(*fmt) ? *fmt++-'0' : 1;
                    loopi(n) putuint(p, va_arg(args, uint));
                    numi += n;
                    break;
                }
                case 'f':
                {
                    int n = isdigit(*fmt) ? *fmt++-'0' : 1;
                    loopi(n) putfloat(p, (float)va_arg(args, double));
                    numi += n;
                    break;
                }
                case 's': sendstring(va_arg(args, const char *), p); nums++; break;
                case 'm':
                {
                    int n = va_arg(args, int);
                    p.put(va_arg(args, uchar *), n);
                    numi += n;
                    break;
                }
            }
            va_end(args);
        }
        int num = nums?0:numi, msgsize = msgsizelookup(type);
        if(msgsize && num != msgsize) { fatal("Inconsistent msg size for %d (%d != %d)", type, num, msgsize); }
        if(reliable) messagereliable = true;
        messages.put(buf, p.length());
        return true;
    }

    void echomsg(int color, const char *s, ...)
    {
        defvformatbigstring(text, s, s);

        gamelog *log = new gamelog(GAMELOG_MESSAGE);
        log->addlist("args", "type", "echomsg");
        log->addlist("args", "text", text);
        log->addlist("args", "colour", color);
        log->addlist("args", "console", text);
        if(!log->push()) DELETEP(log);
    }
    ICOMMAND(0, echomsg, "bs", (int *color, char *str), echomsg(*color >= 0 ? *color : 0xFFFFFF, str));

    void saytext(gameent *f, gameent *t, int flags, char *text)
    {
        bigstring msg, line;
        filterstring(msg, text, true, colourchat ? false : true, true, true);
        if(*filterwords) filterword(msg, filterwords);

        defformatstring(name, "%s", game::colourname(f));
        if(flags&SAY_WHISPER)
        {
            if(!t) return;
            defformatstring(sw, " [\fs\fy%d\fS] (\fs\fcwhispers to %s\fS [\fs\fy%d\fS])", f->clientnum, t == game::player1 ? "you" : game::colourname(t), t->clientnum);
            concatstring(name, sw);
        }
        else if(flags&SAY_TEAM)
        {
            defformatstring(st, " (to team %s)", game::colourteam(f->team));
            concatstring(name, st);
        }
        int color = colourwhite;
        if(flags&SAY_ACTION)
        {
            formatstring(line, "* %s %s", name, msg);
            color = colourmagenta;
        }
        else formatstring(line, "<%s> %s", name, msg);

        char *sndid = NULL;
        ident *wid = idents.access(flags&SAY_ACTION ? "on_action" : "on_text");
        if(wid && wid->type == ID_ALIAS && wid->getstr()[0])
        {
            defformatbigstring(act, "%s %d %d %s %s %s",
                flags&SAY_ACTION ? "on_action" : "on_text", f->clientnum, flags&SAY_TEAM ? 1 : 0,
                escapestring(game::colourname(f)), escapestring(text), escapestring(line));

            sndid = executestr(act);
        }
        if(m_demo(game::gamemode) || ((!(flags&SAY_TEAM) || f->team == game::player1->team) && (!(flags&SAY_WHISPER) || f == game::player1 || t == game::player1)))
        {
            gamelog *log = new gamelog(GAMELOG_MESSAGE);
            log->addlist("args", "type", "chat");
            log->addlist("args", "action", flags);
            log->addlist("args", "sound", sndid && sndid[0] ? sndid : "S_CHAT");
            log->addlist("args", "flags", GAMELOG_F_CLIENT1);
            log->addlist("args", "text", msg);
            log->addlist("args", "colour", color);
            log->addlist("args", "console", line);
            log->addclient("client", f);
            if(t) log->addclient("client", t);
            if(!log->push()) DELETEP(log);
        }

        if(sndid) DELETEA(sndid);

        ai::scanchat(f, t, flags, text);
    }

    void toserver(int flags, const char *text, const char *target)
    {
        if(!waiting(false) && !client::demoplayback)
        {
            bigstring output;
            copystring(output, text, messagelength);
            if(flags&SAY_WHISPER)
            {
                gameent *e = game::getclient(parseplayer(target));
                if(e && e->clientnum != game::player1->clientnum)
                    addmsg(N_TEXT, "ri3s", game::player1->clientnum, e->clientnum, flags, output);
            }
            else
            {
                if(flags&SAY_TEAM && !m_team(game::gamemode, game::mutators))
                    flags &= ~SAY_TEAM;
                addmsg(N_TEXT, "ri3s", game::player1->clientnum, -1, flags, output);
            }
        }
    }
    ICOMMAND(0, say, "C", (char *s), toserver(0, s));
    ICOMMAND(0, me, "C", (char *s), toserver(SAY_ACTION, s));
    ICOMMAND(0, sayteam, "C", (char *s), toserver(SAY_TEAM, s));
    ICOMMAND(0, meteam, "C", (char *s), toserver(SAY_ACTION|SAY_TEAM, s));
    ICOMMAND(IDF_NAMECOMPLETE, whisper, "ss", (char *t, char *s), toserver(SAY_WHISPER, s, t));
    ICOMMAND(IDF_NAMECOMPLETE, mewhisper, "ss", (char *t, char *s), toserver(SAY_ACTION|SAY_WHISPER, s, t));

    void parsecommand(gameent *d, const char *cmd, const char *arg)
    {
        const char *oldval = NULL;
        ident *id = idents.access(cmd);
        if(id && id->flags&IDF_CLIENT)
        {
            const char *val = NULL;
            switch(id->type)
            {
                case ID_COMMAND:
                {
                    #if 0 // these shouldn't get here
                    int slen = strlen(cmd)+1+strlen(arg);
                    char *s = newstring(slen+1);
                    formatstring(s, slen, "%s %s", cmd, arg);
                    char *ret = executestr(s);
                    DELETEA(s);
                    if(ret) echomsg(colourgreen, "%s: \fc%s", cmd, ret);
                    DELETEA(ret);
                    #endif
                    return;
                }
                case ID_VAR:
                {
                    int ret = parseint(arg);
                    oldval = newstring(intstr(id));
                    *id->storage.i = ret;
                    id->changed();
                    val = intstr(id);
                    break;
                }
                case ID_FVAR:
                {
                    float ret = parsefloat(arg);
                    oldval = newstring(floatstr(*id->storage.f));
                    *id->storage.f = ret;
                    id->changed();
                    val = floatstr(*id->storage.f);
                    break;
                }
                case ID_SVAR:
                {
                    oldval = newstring(*id->storage.s);
                    delete[] *id->storage.s;
                    *id->storage.s = newstring(arg);
                    id->changed();
                    val = *id->storage.s;
                    break;
                }
                default: return;
            }
            if((d || showservervariables) && val)
            {
                if(oldval)
                {
                    echomsg(colouryellow, "%s set \fs\fc%s\fS to \fs\fc%s\fS (was: \fs\fc%s\fS)", d ? game::colourname(d) : (connected(false) ? "Server" : "You"), cmd, val, oldval);
                    delete[] oldval;
                }
                else echomsg(colouryellow, "%s set \fs\fc%s\fS to \fs\fc%s\fS", d ? game::colourname(d) : (connected(false) ? "Server" : "You"), cmd, val);
            }
            else if(oldval) delete[] oldval;
        }
        else if(verbose) echomsg(colourred, "%s sent unknown command: \fc%s", d ? game::colourname(d) : "Server", cmd);
    }

    bool sendcmd(int nargs, const char *cmd, const char *arg)
    {
        if(connected(false))
        {
            addmsg(N_COMMAND, "ri2sis", game::player1->clientnum, nargs, cmd, strlen(arg), arg);
            return true;
        }
        else
        {
            defformatstring(scmd, "sv_%s", cmd);
            if(server::servcmd(nargs, scmd, arg))
            {
                if(nargs > 1 && arg) parsecommand(NULL, cmd, arg);
                return true;
            }
        }
        return false;
    }

    void changemapserv(char *name, int gamemode, int mutators, int crc, int variant, int clientnum)
    {
        game::gamestate = G_S_WAITING;
        game::gamemode = gamemode;
        game::mutators = mutators;
        modecheck(game::gamemode, game::mutators);
        game::nextmode = game::gamemode;
        game::nextmuts = game::mutators;
        game::timeremaining = -1;
        game::maptime = game::timesync = game::timelast = game::timeelapsed = 0;
        hud::resetscores();
        mapvotes.shrink(0);
        if(editmode) toggleedit(true);
        if(m_demo(game::gamemode))
        {
            game::maptime = 1;
            game::timeremaining = game::timeelapsed = 0;
            return;
        }
        else if(demoendless) demoendless = 0;
        if(m_capture(game::gamemode)) capture::reset();
        else if(m_defend(game::gamemode)) defend::reset();
        else if(m_bomber(game::gamemode)) bomber::reset();
        needsmap = gettingmap = 0;
        game::updatemusic(10, true);
        switch(clientnum)
        {
            case -3: break; // welcome packet or command line
            case -2:
                echomsg(colourwhite, "Vote passed: \fs\fy%s\fS on \fs\fo%s\fS", server::gamename(game::gamemode, game::mutators), mapctitle(name));
                break;
            case -1:
                echomsg(colourwhite, "Server chooses: \fs\fy%s\fS on \fs\fo%s\fS", server::gamename(game::gamemode, game::mutators), mapctitle(name));
                break;
            default:
            {
                gameent *d = game::getclient(clientnum);
                if(!d) break;
                echomsg(colourwhite, "%s chooses: \fs\fy%s\fS on \fs\fo%s\fS", game::colourname(d), server::gamename(game::gamemode, game::mutators), mapctitle(name));
            }
        }
        if(crc < -1 || !name || !*name || !load_world(name, crc, variant))
        {
            switch(crc)
            {
                case -1:
                    if(!mapcrc) emptymap(0, true, name);
                    needsmap = gettingmap = 0;
                    loadedmap = sendcrcinfo = true; // the server wants us to start
                    break;
                case -2:
                    conoutf(colourwhite, "Waiting for server to request the map..");
                default:
                    emptymap(0, true, name);
                    needsmap = totalmillis;
                    if(crc > 0) addmsg(N_GETMAP, "r");
                    break;
            }
        }
        else
        {
            needsmap = gettingmap = 0;
            loadedmap = sendcrcinfo = true;
        }
        if(m_capture(game::gamemode)) capture::setup();
        else if(m_defend(game::gamemode)) defend::setup();
        else if(m_bomber(game::gamemode)) bomber::setup();
    }

    void receivefile(uchar *data, int len)
    {
        ucharbuf p(data, len);
        int type = getint(p);
        switch(type)
        {
            case N_SENDDEMO:
            {
                int ctime = getint(p);
                int nameid = getint(p);
                if(filetimelocal) ctime += clockoffset;
                data += p.length();
                len -= p.length();
                string fname;
                const char *demoname = demonames.find(nameid, "");
                if(*demoname)
                {
                    formatstring(fname, "demos/%s.dmo", demoname);
                    DELETEA(demoname);
                }
                else
                {
                    if(*filetimeformat) formatstring(fname, "demos/%s.dmo", gettime(ctime, filetimeformat));
                    else formatstring(fname, "demos/%u.dmo", uint(ctime));
                }
                stream *demo = openfile(fname, "wb");
                if(!demo) return;
                demo->write(data, len);
                delete demo;
                conoutf(colouryellow, "Received demo: \fc%s", fname);
                scandemo(fname);
                break;
            }

            case N_SENDMAPFILE:
            {
                int filetype = getint(p), filecrc = getint(p);
                string fname;
                gettingmap = totalmillis;
                getstring(fname, p);
                data += p.length();
                len -= p.length();
                if(filetype < 0 || filetype >= SENDMAP_MAX || len <= 0) break;
                if(!*fname) copystring(fname, "maps/untitled");
                // Remove any temp/ prefix from file name before testing and rebuilding.
                defformatstring(nfname, "%s", (strstr(fname, "temp/") == fname || strstr(fname, "temp\\") == fname) ? fname + 5 : fname);
                defformatstring(ffile, strstr(nfname, "maps/") == nfname || strstr(nfname, "maps\\") == nfname ? "temp/%s_0x%.8x" : "temp/maps/%s_0x%.8x", nfname, filecrc);
                defformatstring(ffext, "%s.%s", ffile, sendmaptypes[filetype]);
                stream *f = openfile(ffext, "wb");
                if(!f)
                {
                    conoutf(colourred, "Failed to open map file: \fc%s", ffext);
                    break;
                }
                f->write(data, len);
                delete f;
                conoutf(colouryellow, "Wrote map file: \fc%s (%d %s) [0x%.8x]", ffext, len, len != 1 ? "bytes" : "byte", filecrc);
                break;
            }
            default: break;
        }
    }
    ICOMMAND(0, getmap, "", (), if(multiplayer(false)) addmsg(N_GETMAP, "r"));

    void stopdemo()
    {
        if(remote) addmsg(N_STOPDEMO, "r");
        else server::stopdemo();
    }
    ICOMMAND(0, stopdemo, "", (), stopdemo());

    void recorddemo(int val)
    {
        addmsg(N_RECORDDEMO, "ri", val);
    }
    ICOMMAND(0, recorddemo, "i", (int *val), recorddemo(*val));

    void cleardemos(int val)
    {
        addmsg(N_CLEARDEMOS, "ri", val);
    }
    ICOMMAND(0, cleardemos, "i", (int *val), cleardemos(*val));

    void getdemo(int i, const char *name)
    {
        if(i <= 0) conoutf(colouryellow, "Getting demo, please wait..");
        else conoutf(colouryellow, "Getting demo \fs\fc%d\fS, please wait..", i);
        addmsg(N_GETDEMO, "ri2", i, demonameid);
        if(*name) demonames.access(demonameid, newstring(name));
        demonameid++;
    }
    ICOMMAND(0, getdemo, "is", (int *val, char *name), getdemo(*val, name));

    void listdemos()
    {
        conoutf(colouryellow, "Listing demos..");
        addmsg(N_LISTDEMOS, "r");
    }
    ICOMMAND(0, listdemos, "", (), listdemos());

    void changemap(const char *name) // request map change, server may ignore
    {
        int nextmode = game::nextmode, nextmuts = game::nextmuts; // in case stopdemo clobbers these
        if(!remote) stopdemo();
        if(!connected())
        {
            server::changemap(name, nextmode, nextmuts);
            localconnect(true);
        }
        else
        {
            stringz(reqfile);
            if(name && *name)
                copystring(reqfile, !strncasecmp(name, "temp/", 5) || !strncasecmp(name, "temp\\", 5) ? name+5 : name);
            else copystring(reqfile, "<random>");
            addmsg(N_MAPVOTE, "rsi2", reqfile, nextmode, nextmuts);
        }
    }
    ICOMMAND(IDF_NOECHO, map, "s", (char *s), changemap(s));
    ICOMMAND(0, clearvote, "", (), addmsg(N_CLEARVOTE, "r"));

    void sendmap()
    {
        conoutf(colouryellow, "Sending map..");
        const char *reqmap = mapname;
        if(!reqmap || !*reqmap) reqmap = "maps/untitled";
        bool saved = false;
        if(m_edit(game::gamemode))
        {
            save_world(mapname, m_edit(game::gamemode), true);
            ai::savewaypoints(true, mapname);
            reqmap = mapname;
            saved = true;
        }
        loopi(SENDMAP_MAX)
        {
            defformatstring(reqfext, "%s.%s", reqmap, sendmaptypes[i]);
            stream *f = openfile(reqfext, "rb");
            if(f)
            {
                conoutf(colouryellow, "Transmitting file: \fc%s", reqfext);
                sendfile(-1, 2, f, "ri3", N_SENDMAPFILE, i, mapcrc);
                if(needclipboard >= 0) needclipboard++;
                delete f;
            }
            else
            {
                conoutf(colourred, "Failed to open map file: \fc%s", reqfext);
                sendfile(-1, 2, NULL, "ri3", N_SENDMAPFILE, i, mapcrc);
            }
        }
        if(saved) setnames(mapname);
    }
    ICOMMAND(0, sendmap, "", (), if(multiplayer(false)) sendmap());

    void gotoplayer(const char *arg)
    {
        if(game::player1->state != CS_SPECTATOR && game::player1->state != CS_EDITING) return;
        int i = parseplayer(arg);
        if(i >= 0 && i != game::player1->clientnum)
        {
            gameent *d = game::getclient(i);
            if(!d) return;
            game::player1->o = d->o;
            vec dir(game::player1->yaw, game::player1->pitch);
            game::player1->o.add(dir.mul(-32));
            game::player1->resetinterp(true);
            game::resetcamera();
        }
    }
    ICOMMAND(IDF_NAMECOMPLETE, goto, "s", (char *s), gotoplayer(s));

    void editvar(ident *id, bool local)
    {
        if(id && id->flags&IDF_MAP && !(id->flags&IDF_META) && !(id->flags&IDF_SERVER) && local && m_edit(game::gamemode) && game::player1->state == CS_EDITING)
        {
            switch(id->type)
            {
                case ID_VAR:
                    addmsg(N_EDITVAR, "riisi", id->type, id->flags&IDF_TX_MASK, id->name, *id->storage.i);
                    // echomsg(colouryellow, "%s set map variable \fs\fc%s\fS to \fs\fc%s\fS", game::colourname(game::player1), id->name, intstr(id));
                    break;
                case ID_FVAR:
                    addmsg(N_EDITVAR, "riisf", id->type, id->flags&IDF_TX_MASK, id->name, *id->storage.f);
                    // echomsg(colouryellow, "%s set map variable \fs\fc%s\fS to \fs\fc%s\fS", game::colourname(game::player1), id->name, floatstr(*id->storage.f));
                    break;
                case ID_SVAR:
                    addmsg(N_EDITVAR, "riisis", id->type, id->flags&IDF_TX_MASK, id->name, strlen(*id->storage.s), *id->storage.s);
                    // echomsg(colouryellow, "%s set map variable \fs\fc%s\fS to \fy\fc%s\fS", game::colourname(game::player1), id->name, *id->storage.s);
                    break;
                case ID_ALIAS:
                {
                    const char *s = id->getstr();
                    addmsg(N_EDITVAR, "riisis", id->type, id->flags&IDF_TX_MASK, id->name, strlen(s), s);
                    // echomsg(colouryellow, "%s set map alias \fs\fc%s\fS to \fs\fc%s\fS", game::colourname(game::player1), id->name, s);
                    break;
                }
                default: break;
            }
        }
    }

    void sendclipboard()
    {
        uchar *outbuf = NULL;
        int inlen = 0, outlen = 0;
        if(!packeditinfo(localedit, inlen, outbuf, outlen))
        {
            outbuf = NULL;
            inlen = outlen = 0;
            needclipboard = -1;
        }
        else needclipboard = 0;
        packetbuf p(16 + outlen, ENET_PACKET_FLAG_RELIABLE);
        putint(p, N_CLIPBOARD);
        putint(p, inlen);
        putint(p, outlen);
        if(outlen > 0) p.put(outbuf, outlen);
        sendclientpacket(p.finalize(), 1);
    }

    void edittrigger(const selinfo &sel, int op, int arg1, int arg2, int arg3, const VSlot *vs)
    {
        if(m_edit(game::gamemode) && game::player1->state == CS_EDITING) switch(op)
        {
            case EDIT_FLIP:
            case EDIT_COPY:
            case EDIT_PASTE:
            case EDIT_DELCUBE:
            {
                switch(op)
                {
                    case EDIT_COPY: needclipboard = arg1; break; // 0 - has clipboard; 1 - has clipboard with unknown geometry
                    case EDIT_PASTE:
                        if(needclipboard > 0)
                        {
                            c2sinfo(true);
                            sendclipboard();
                        }
                        break;
                }
                addmsg(N_EDITF + op, "ri9i4",
                    sel.o.x, sel.o.y, sel.o.z, sel.s.x, sel.s.y, sel.s.z, sel.grid, sel.orient,
                    sel.cx, sel.cxs, sel.cy, sel.cys, sel.corner);
                break;
            }
            case EDIT_MAT:
            {
                addmsg(N_EDITF + op, "ri9i7",
                    sel.o.x, sel.o.y, sel.o.z, sel.s.x, sel.s.y, sel.s.z, sel.grid, sel.orient,
                    sel.cx, sel.cxs, sel.cy, sel.cys, sel.corner,
                    arg1, arg2, arg3);
                break;
            }
            case EDIT_ROTATE:
            {
                addmsg(N_EDITF + op, "ri9i5",
                    sel.o.x, sel.o.y, sel.o.z, sel.s.x, sel.s.y, sel.s.z, sel.grid, sel.orient,
                    sel.cx, sel.cxs, sel.cy, sel.cys, sel.corner,
                    arg1);
                break;
            }
            case EDIT_FACE:
            {
                addmsg(N_EDITF + op, "ri9i6",
                    sel.o.x, sel.o.y, sel.o.z, sel.s.x, sel.s.y, sel.s.z, sel.grid, sel.orient,
                    sel.cx, sel.cxs, sel.cy, sel.cys, sel.corner,
                    arg1, arg2);
                break;
            }
            case EDIT_TEX:
            {
                int tex1 = shouldpacktex(arg1);
                if(addmsg(N_EDITF + op, "ri9i6",
                    sel.o.x, sel.o.y, sel.o.z, sel.s.x, sel.s.y, sel.s.z, sel.grid, sel.orient,
                    sel.cx, sel.cxs, sel.cy, sel.cys, sel.corner,
                    tex1 ? tex1 : arg1, arg2))
                {
                    messages.pad(2);
                    int offset = messages.length();
                    if(tex1) packvslot(messages, arg1);
                    *(ushort *)&messages[offset-2] = lilswap(ushort(messages.length() - offset));
                }
                break;
            }
            case EDIT_REPLACE:
            {
                int tex1 = shouldpacktex(arg1), tex2 = shouldpacktex(arg2);
                if(addmsg(N_EDITF + op, "ri9i7",
                    sel.o.x, sel.o.y, sel.o.z, sel.s.x, sel.s.y, sel.s.z, sel.grid, sel.orient,
                    sel.cx, sel.cxs, sel.cy, sel.cys, sel.corner,
                    tex1 ? tex1 : arg1, tex2 ? tex2 : arg2, arg3))
                {
                    messages.pad(2);
                    int offset = messages.length();
                    if(tex1) packvslot(messages, arg1);
                    if(tex2) packvslot(messages, arg2);
                    *(ushort *)&messages[offset-2] = lilswap(ushort(messages.length() - offset));
                }
                break;
            }
            case EDIT_CALCLIGHT:
            case EDIT_REMIP:
            {
                addmsg(N_EDITF + op, "r");
                break;
            }
            case EDIT_VSLOT:
            {
                if(addmsg(N_EDITF + op, "ri9i6",
                    sel.o.x, sel.o.y, sel.o.z, sel.s.x, sel.s.y, sel.s.z, sel.grid, sel.orient,
                    sel.cx, sel.cxs, sel.cy, sel.cys, sel.corner,
                    arg1, arg2))
                {
                    messages.pad(2);
                    int offset = messages.length();
                    packvslot(messages, vs);
                    *(ushort *)&messages[offset-2] = lilswap(ushort(messages.length() - offset));
                }
                break;
            }
            case EDIT_UNDO:
            case EDIT_REDO:
            {
                uchar *outbuf = NULL;
                int inlen = 0, outlen = 0;
                if(packundo(op, inlen, outbuf, outlen))
                {
                    if(addmsg(N_EDITF + op, "ri2", inlen, outlen)) messages.put(outbuf, outlen);
                    delete[] outbuf;
                }
                break;
            }
        }
    }

    void sendintro()
    {
        packetbuf p(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);

        putint(p, N_CONNECT);

        sendstring(game::player1->name, p);
        loopk(2) putint(p, game::player1->colours[k]);
        putint(p, game::player1->model);
        sendstring(game::player1->vanity, p);
        sendstring(game::player1->mixer, p);
        putint(p, game::player1->loadweap.length());
        loopv(game::player1->loadweap) putint(p, game::player1->loadweap[i]);
        putint(p, game::player1->randweap.length());
        loopv(game::player1->randweap) putint(p, game::player1->randweap[i]);

        stringz(hash);
        if(connectpass[0])
        {
            server::hashpassword(game::player1->clientnum, sessionid, connectpass, hash);
            memset(connectpass, 0, sizeof(connectpass));
        }
        sendstring(hash, p);
        sendstring(authconnect ? accountname : "", p);

        game::player1->version.put(p);

        sendclientpacket(p.finalize(), 1);
    }

    static void sendposition(gameent *d, packetbuf &q)
    {
        inanimate *m = entities::currentpassenger(d);
        int pid = m ? m->findpassenger(d) : -1;
        putint(q, N_POS);
        putuint(q, d->clientnum);
        // 3 bits phys state, 2 bits move, 2 bits strafe, 2 bits turnside
        uint physstate = d->physstate | ((d->move&3)<<3) | ((d->strafe&3)<<5) | ((d->turnside&3)<<7);
        putuint(q, physstate);
        putuint(q, d->impulse[IM_METER]);
        ivec o = ivec(vec(d->feetpos()).mul(DMF)), f = ivec(vec(d->floorpos).mul(DMF)), n = m ? ivec(vec(m->passengers[pid].offset).mul(DMF)) : ivec(0, 0, 0);
        uint vel = min(int(d->vel.magnitude()*DVELF), 0xFFFF), fall = min(int(d->falling.magnitude()*DVELF), 0xFFFF);
        // 3 bits position, 3 bits floor, 1 bit velocity, 3 bits falling, 1 bit conopen, X bits actions
        uint flags = 0;
        if(o.x < 0 || o.x > 0xFFFF) flags |= 1<<0;
        if(o.y < 0 || o.y > 0xFFFF) flags |= 1<<1;
        if(o.z < 0 || o.z > 0xFFFF) flags |= 1<<2;
        if(f.x < 0 || f.x > 0xFFFF) flags |= 1<<3;
        if(f.y < 0 || f.y > 0xFFFF) flags |= 1<<4;
        if(f.z < 0 || f.z > 0xFFFF) flags |= 1<<5;
        if(vel > 0xFF) flags |= 1<<6;
        if(fall > 0)
        {
            flags |= 1<<7;
            if(fall > 0xFF) flags |= 1<<8;
            if(d->falling.x || d->falling.y || d->falling.z > 0) flags |= 1<<9;
        }
        if(d->conopen) flags |= 1<<10;
        if(d->forcepos)
        {
            flags |= 1<<11;
            d->forcepos = false;
        }
        if(m)
        {
            flags |= 1<<12;
            if(n.x < 0 || n.x > 0xFFFF) flags |= 1<<13;
            if(n.y < 0 || n.y > 0xFFFF) flags |= 1<<14;
            if(n.z < 0 || n.z > 0xFFFF) flags |= 1<<15;
        }
        loopk(AC_MAX) if(d->action[k]) flags |= 1<<(16+k);
        putuint(q, flags);
        loopk(3)
        {
            q.put(o[k]&0xFF);
            q.put((o[k]>>8)&0xFF);
            if(o[k] < 0 || o[k] > 0xFFFF) q.put((o[k]>>16)&0xFF);
        }
        loopk(3)
        {
            q.put(f[k]&0xFF);
            q.put((f[k]>>8)&0xFF);
            if(f[k] < 0 || f[k] > 0xFFFF) q.put((f[k]>>16)&0xFF);
        }
        if(m)
        {
            putuint(q, m->ent);
            loopk(3)
            {
                q.put(n[k]&0xFF);
                q.put((n[k]>>8)&0xFF);
                if(n[k] < 0 || n[k] > 0xFFFF) q.put((n[k]>>16)&0xFF);
            }
        }
        float yaw = d->yaw, pitch = d->pitch;
        if(d == game::player1 && game::thirdpersonview(true, d))
        {
            vectoyawpitch(vec(worldpos).sub(d->headpos()).normalize(), yaw, pitch);
            fixrange(yaw, pitch);
        }
        uint dir = (yaw < 0 ? 360 + int(yaw)%360 : int(yaw)%360) + clamp(int(pitch+90), 0, 180)*360;
        q.put(dir&0xFF);
        q.put((dir>>8)&0xFF);
        q.put(clamp(int(d->roll+90), 0, 180));
        q.put(vel&0xFF);
        if(vel > 0xFF) q.put((vel>>8)&0xFF);
        float velyaw, velpitch;
        vectoyawpitch(d->vel, velyaw, velpitch);
        uint veldir = (velyaw < 0 ? 360 + int(velyaw)%360 : int(velyaw)%360) + clamp(int(velpitch+90), 0, 180)*360;
        q.put(veldir&0xFF);
        q.put((veldir>>8)&0xFF);
        if(fall > 0)
        {
            q.put(fall&0xFF);
            if(fall > 0xFF) q.put((fall>>8)&0xFF);
            if(d->falling.x || d->falling.y || d->falling.z > 0)
            {
                float fallyaw, fallpitch;
                vectoyawpitch(d->falling, fallyaw, fallpitch);
                uint falldir = (fallyaw < 0 ? 360 + int(fallyaw)%360 : int(fallyaw)%360) + clamp(int(fallpitch+90), 0, 180)*360;
                q.put(falldir&0xFF);
                q.put((falldir>>8)&0xFF);
            }
        }
    }

    void sendpositions()
    {
        gameent *d = NULL;
        int numdyns = game::numdynents();
        loopi(numdyns) if((d = (gameent *)game::iterdynents(i)))
        {
            if((d == game::player1 || d->ai) && (d->state == CS_ALIVE || d->state == CS_EDITING))
            {
                packetbuf q(100);
                sendposition(d, q);
                for(int j = i+1; j < numdyns; j++)
                {
                    gameent *e = (gameent *)game::iterdynents(j);
                    if(e && (e == game::player1 || e->ai) && (e->state == CS_ALIVE || e->state == CS_EDITING))
                        sendposition(e, q);
                }
                sendclientpacket(q.finalize(), 0);
                break;
            }
        }
    }

    void sendmessages()
    {
        packetbuf p(MAXTRANS);
        if(isready)
        {
            if(sendplayerinfo && (!lastplayerinfo || totalmillis-lastplayerinfo >= setinfowait))
            {
                p.reliable();
                sendplayerinfo = false;
                lastplayerinfo = totalmillis ? totalmillis : 1;
                putint(p, N_SETPLAYERINFO); // name colour model checkpoint vanity mixer count <loadweaps> count <randweaps>
                sendstring(game::player1->name, p);
                loopk(2) putint(p, game::player1->colours[k]);
                putint(p, game::player1->model);
                putint(p, game::player1->checkpointspawn);
                sendstring(game::player1->vanity, p);
                sendstring(game::player1->mixer, p);
                putint(p, game::player1->loadweap.length());
                loopv(game::player1->loadweap) putint(p, game::player1->loadweap[i]);
                putint(p, game::player1->randweap.length());
                loopv(game::player1->randweap) putint(p, game::player1->randweap[i]);
            }
            if(sendcrcinfo)
            {
                p.reliable();
                putint(p, N_MAPCRC);
                sendstring(mapname, p);
                putint(p, mapcrc);
                sendcrcinfo = false;
            }
            if(sendgameinfo)
            {
                p.reliable();
                putint(p, N_GAMEINFO);
                enumerate(idents, ident, id,
                {
                    if(id.flags&IDF_CLIENT && id.flags&IDF_MAP) switch(id.type)
                    {
                        case ID_VAR:
                            putint(p, id.type);
                            sendstring(id.name, p);
                            putint(p, *id.storage.i);
                            break;
                        case ID_FVAR:
                            putint(p, id.type);
                            sendstring(id.name, p);
                            putfloat(p, *id.storage.f);
                            break;
                        case ID_SVAR:
                            putint(p, id.type);
                            sendstring(id.name, p);
                            sendstring(*id.storage.s, p);
                            break;
                        default: break;
                    }
                });
                putint(p, -1);
                entities::putitems(p);
                putint(p, -1);
                if(m_capture(game::gamemode)) capture::sendaffinity(p);
                else if(m_defend(game::gamemode)) defend::sendaffinity(p);
                else if(m_bomber(game::gamemode)) bomber::sendaffinity(p);
                sendgameinfo = false;
            }
            if(gs_playing(game::gamestate) && needsmap && !gettingmap && totalmillis-needsmap >= 30000)
            {
                p.reliable();
                putint(p, N_GETMAP);
                needsmap = totalmillis;
            }
        }
        if(messages.length())
        {
            p.put(messages.getbuf(), messages.length());
            messages.setsize(0);
            if(messagereliable) p.reliable();
            messagereliable = false;
        }
        if(totalmillis-lastping>250)
        {
            putint(p, N_PING);
            putint(p, totalmillis);
            lastping = totalmillis ? totalmillis : 1;
        }

        sendclientpacket(p.finalize(), 1);
    }

    void c2sinfo(bool force) // send update to the server
    {
        static int lastupdate = -1000;
        if(totalmillis-lastupdate < 40 && !force) return;    // don't update faster than 25fps
        lastupdate = totalmillis ? totalmillis : 1;
        sendpositions();
        sendmessages();
        flushclient();
    }

    bool parsestate(gameent *d, ucharbuf &p, bool resume = false)
    {
        if(!d) { static gameent dummy; d = &dummy; }
        bool local = d == game::player1 || d->ai, reset = false;
        if(!local || !resume) d->respawn(lastmillis, game::gamemode, game::mutators);
        int state = getint(p);
        if(state == -1) reset = true;
        else if(!local) d->state = state;
        d->points = getint(p);
        d->frags = getint(p);
        d->deaths = getint(p);
        d->totalpoints = getint(p);
        d->totalfrags = getint(p);
        d->totaldeaths = getint(p);
        d->timeplayed = getint(p);
        d->lasttimeplayed = totalmillis ? totalmillis : 1;
        d->health = getint(p);
        d->cptime = getint(p);
        if(local && resume && !reset)
        {
            d->weapreset(false);
            getint(p);
            loopi(W_MAX) loopj(W_A_MAX) getint(p);
            loopi(W_MAX) getint(p);
        }
        else
        {
            d->weapreset(!resume);
            int weap = getint(p);
            d->weapselect = isweap(weap) ? weap : W_CLAW;
            loopi(W_MAX) loopj(W_A_MAX) d->weapammo[i][j] = getint(p);
            loopi(W_MAX) d->weapent[i] = getint(p);
        }
        if(resume) d->configure(lastmillis, game::gamemode, game::mutators, physics::hasaffinity(d));
        return reset;
    }

    void updatepos(gameent *d)
    {
        // update the position of other clients in the game in our world
        // don't care if he's in the scenery or other players, just don't overlap with our client
        const float r = game::player1->radius+d->radius;
        const float dx = game::player1->o.x-d->o.x;
        const float dy = game::player1->o.y-d->o.y;
        const float dz = game::player1->o.z-d->o.z;
        const float rz = game::player1->aboveeye+game::player1->height;
        const float fx = (float)fabs(dx), fy = (float)fabs(dy), fz = (float)fabs(dz);
        if(fx < r && fy < r && fz < rz && d->state != CS_SPECTATOR && d->state != CS_WAITING && d->state != CS_DEAD)
        {
            if(fx < fy) d->o.y += dy < 0 ? r-fy : -(r-fy);  // push aside
            else        d->o.x += dx < 0 ? r-fx : -(r-fx);
        }
        int lagtime = totalmillis-d->lastupdate;
        if(lagtime)
        {
            if(d->lastupdate) d->plag = (d->plag*5+lagtime)/6;
            d->lastupdate = totalmillis ? totalmillis : 1;
        }
    }

    void parsepositions(ucharbuf &p)
    {
        int type;
        while(p.remaining()) switch(type = getint(p))
        {
            case N_POS: // position of another client
            {
                int lcn = getuint(p), physstate = getuint(p), meter = getuint(p), flags = getuint(p);
                gameent *d = game::getclient(lcn);
                vec o, f, n, vel, falling;
                float yaw, pitch, roll;
                loopk(3)
                {
                    int z = p.get();
                    z |= p.get()<<8;
                    if(flags&(1<<k))
                    {
                        z |= p.get()<<16;
                        if(z&0x800000) z |= ~0U<<24;
                    }
                    o[k] = z/DMF;
                }
                loopk(3)
                {
                    int z = p.get();
                    z |= p.get()<<8;
                    if(flags&(1<<(k+3)))
                    {
                        z |= p.get()<<16;
                        if(z&0x800000) z |= ~0U<<24;
                    }
                    f[k] = z/DMF;
                }
                inanimate *m = NULL;
                if(flags&(1<<12))
                {
                    int ent = getuint(p);
                    loopk(3)
                    {
                        int z = p.get();
                        z |= p.get()<<8;
                        if(flags&(1<<(k+13)))
                        {
                            z |= p.get()<<16;
                            if(z&0x800000) z |= ~0U<<24;
                        }
                        n[k] = z/DMF;
                    }
                    m = entities::remotepassenger(ent, d, n);
                }
                int dir = p.get();
                dir |= p.get()<<8;
                yaw = dir%360;
                pitch = clamp(dir/360, 0, 180)-90;
                roll = clamp(int(p.get()), 0, 180)-90;
                int mag = p.get();
                if(flags&(1<<6)) mag |= p.get()<<8;
                dir = p.get();
                dir |= p.get()<<8;
                vecfromyawpitch(dir%360, clamp(dir/360, 0, 180)-90, 1, 0, vel);
                vel.mul(mag/DVELF);
                if(flags&(1<<7))
                {
                    mag = p.get();
                    if(flags&(1<<8)) mag |= p.get()<<8;
                    if(flags&(1<<9))
                    {
                        dir = p.get();
                        dir |= p.get()<<8;
                        vecfromyawpitch(dir%360, clamp(dir/360, 0, 180)-90, 1, 0, falling);
                    }
                    else falling = vec(0, 0, -1);
                    falling.mul(mag/DVELF);
                }
                else falling = vec(0, 0, 0);
                if(!d || d == game::player1 || d->ai) continue;
                float oldyaw = d->yaw, oldpitch = d->pitch;
                d->yaw = yaw;
                d->pitch = pitch;
                d->roll = roll;
                d->move = (physstate>>3)&2 ? -1 : (physstate>>3)&1;
                d->strafe = (physstate>>5)&2 ? -1 : (physstate>>5)&1;
                d->turnside = (physstate>>7)&2 ? -1 : (physstate>>7)&1;
                d->impulse[IM_METER] = meter;
                d->conopen = flags&(1<<10) ? true : false;
                d->forcepos = flags&(1<<11) ? true : false;
                loopk(AC_MAX)
                {
                    bool val = d->action[k];
                    d->action[k] = flags&(1<<(16+k)) ? true : false;
                    if(val != d->action[k])
                    {
                        if(d->action[k]) d->actiontime[k] = lastmillis;
                        else if(k == AC_CROUCH || k == AC_JUMP) d->actiontime[k] = -lastmillis;
                    }
                }
                vec oldpos(d->o);
                if(m) d->o = vec(m->o).add(n);
                else
                {
                    entities::removepassenger(d);
                    d->o = o;
                    d->o.z += d->height;
                }
                d->floorpos = f;
                d->vel = vel;
                d->falling = falling;
                d->physstate = physstate&7;
                physics::updatephysstate(d);
                updatepos(d);
                if(d->forcepos || d->state == CS_DEAD || d->state == CS_WAITING)
                {
                    d->resetinterp();
                    d->smoothmillis = 0;
                    d->forcepos = false;
                }
                else if(d->respawned < 0)
                {
                    d->resetinterp();
                    d->smoothmillis = 0;
                    game::respawned(d, false);
                    d->respawned = lastmillis;
                }
                else if(physics::smoothmove && d->smoothmillis >= 0 && oldpos.dist(d->o) < physics::smoothdist)
                {
                    d->newpos = d->o;
                    d->newpos.z -= d->height;
                    d->newyaw = d->yaw;
                    d->newpitch = d->pitch;

                    d->o = oldpos;
                    d->yaw = oldyaw;
                    d->pitch = oldpitch;

                    oldpos.z -= d->height;
                    (d->deltapos = oldpos).sub(d->newpos);

                    d->deltayaw = oldyaw - d->newyaw;
                    if(d->deltayaw > 180) d->deltayaw -= 360;
                    else if(d->deltayaw < -180) d->deltayaw += 360;
                    d->deltapitch = oldpitch - d->newpitch;

                    d->smoothmillis = lastmillis;
                }
                else d->smoothmillis = 0;
                break;
            }

            default:
                neterr("type");
                return;
        }
    }

    void parsemessages(int cn, gameent *d, ucharbuf &p)
    {
        static char text[MAXTRANS];
        int type = -1, prevtype = -1;

        while(p.remaining())
        {
            prevtype = type;
            type = getint(p);
            if(debugmessages) conoutf(colourwhite, "[client] msg: %d, prev: %d", type, prevtype);
            switch(type)
            {
                case N_SERVERINIT: // welcome messsage from the server
                {
                    game::player1->clientnum = getint(p);
                    sessionver = getint(p);
                    getstring(game::player1->hostip, p);
                    sessionid = getint(p);
                    if(sessionver != VERSION_GAME)
                    {
                        conoutf(colourred, "Error: this server is running an incompatible protocol (%d v %d)", sessionver, VERSION_GAME);
                        disconnect();
                        return;
                    }
                    sessionflags = getint(p);
                    conoutf(colourwhite, "Connected, starting negotiation with server");
                    sendintro();
                    break;
                }
                case N_WELCOME:
                    mastermode = getint(p);
                    conoutf(colourwhite, "Negotiation complete, \fs\fcmastermode\fS is \fs\fc%d\fS (\fs\fc%s\fS)", mastermode, mastermodename(mastermode));
                    isready = true;
                    break;

                case N_CLIENT:
                {
                    int lcn = getint(p), len = getuint(p);
                    ucharbuf q = p.subbuf(len);
                    gameent *t = game::getclient(lcn);
                    parsemessages(lcn, t, q);
                    break;
                }

                case N_SPHY: // simple phys events
                {
                    int lcn = getint(p), st = getint(p);
                    gameent *t = game::getclient(lcn);
                    bool proceed = t && ((SPHY_SERVER&(1<<st)) || (t != game::player1 && !t->ai));
                    switch(st)
                    {
                        case SPHY_JUMP:
                        {
                            if(!proceed) break;
                            t->doimpulse(IM_T_JUMP, lastmillis);

                            static fx::FxHandle fx = fx::getfxhandle("FX_PLAYER_JUMP");
                            fx::createfx(fx).setentity(t).setcolor(bvec(game::getcolour(t)));
                            break;
                        }
                        case SPHY_BOOST: case SPHY_DASH: case SPHY_POUND: case SPHY_SLIDE: case SPHY_LAUNCH: case SPHY_MELEE: case SPHY_KICK: case SPHY_GRAB: case SPHY_WALLRUN: case SPHY_VAULT:
                        {
                            int param = st == SPHY_VAULT ? 0 : 1;
                            if(st == SPHY_WALLRUN) param = getint(p);
                            if(!proceed) break;
                            t->doimpulse(IM_T_BOOST + (st - SPHY_BOOST), lastmillis);
                            if(param != 0) game::impulseeffect(t);
                            break;
                        }
                        case SPHY_EXTINGUISH:
                        {
                            if(!proceed) break;
                            t->resetresidual(W_R_BURN);

                            static fx::FxHandle fx = fx::getfxhandle("FX_PLAYER_EXTINGUISH");
                            fx::createfx(fx).setentity(t).setcolor(bvec(game::getcolour(t)));
                            break;
                        }
                        case SPHY_BUFF:
                        {
                            int param = getint(p);
                            if(!proceed) break;
                            t->lastbuff = param ? lastmillis : 0;
                            break;
                        }
                        case SPHY_PRIZE:
                        {
                            int param = getint(p);
                            t->collectprize(param);
                            break;
                        }
                        case SPHY_HACKED:
                        {
                            int param = getint(p);
                            if(t->ai) ai::hacked(t, param);
                            t->lasthacker = param;
                            break;
                        }
                        default: break;
                    }
                    break;
                }

                case N_GAMELOG: gamelog::parselog(p); break;

                case N_TEXT:
                {
                    int fcn = getint(p), tcn = getint(p), flags = getint(p);
                    getstring(text, p);
                    gameent *fcp = game::getclient(fcn);
                    gameent *tcp = game::getclient(tcn);
                    if(!fcp || isignored(fcp->clientnum) || isignored(fcp->ownernum)) break;
                    saytext(fcp, tcp, flags, text);
                    break;
                }

                case N_COMMAND:
                {
                    int lcn = getint(p);
                    gameent *f = lcn >= 0 ? game::getclient(lcn) : NULL;
                    getstring(text, p);
                    int alen = getint(p);
                    if(alen < 0 || alen > p.remaining()) break;
                    char *arg = newstring(alen);
                    getstring(arg, p, alen+1);
                    parsecommand(f, text, arg);
                    delete[] arg;
                    break;
                }

                case N_EXECLINK:
                {
                    int tcn = getint(p), index = getint(p);
                    gameent *t = game::getclient(tcn);
                    if(!t || !d || (t->clientnum != d->clientnum && t->ownernum != d->clientnum) || t == game::player1 || t->ai) break;
                    entities::execlink(t, index, false);
                    break;
                }

                case N_MAPCHANGE:
                {
                    getstring(text, p);
                    int mode = getint(p), muts = getint(p), crc = getint(p), variant = clamp(getint(p), int(MPV_DEFAULT), int(MPV_MAX-1)), tcn = getint(p);
                    if(crc >= 0) conoutf(colourwhite, "Map change: %s (%d:%d) [0x%.8x] (%s)", text, mode, muts, crc, MPV_STR[variant]);
                    else conoutf(colourwhite, "Map change: %s (%d:%d) [NOCRC] (%s)", text, mode, muts, MPV_STR[variant]);
                    changemapserv(text, mode, muts, crc, variant, tcn);
                    break;
                }

                case N_GETGAMEINFO:
                {
                    conoutf(colourwhite, "Sending game info..");
                    sendgameinfo = true;
                    break;
                }

                case N_GAMEINFO:
                {
                    int n;
                    sendgameinfo = false;
                    while(p.remaining() && (n = getint(p)) != -1) entities::setspawn(n, getint(p));
                    break;
                }

                case N_GAMESERVINFO:
                {
                    triggerid = getint(p);
                    break;
                }

                case N_ATTRMAP:
                {
                    loopi(W_MAX) game::attrmap[i] = getint(p);
                    break;
                }

                case N_SETPLAYERINFO: // name colour model checkpoint vanity mixer count <loadweaps> count <randweaps>
                {
                    getstring(text, p);
                    int c1 = getint(p), c2 = getint(p), model = getint(p), cps = getint(p);
                    stringz(vanity);
                    getstring(vanity, p);
                    stringz(mixer);
                    getstring(mixer, p);
                    int lw = getint(p);
                    vector<int> lweaps;
                    loopk(lw) lweaps.add(getint(p));
                    int rw = getint(p);
                    vector<int> rweaps;
                    loopk(rw) rweaps.add(getint(p));
                    if(!d) break;
                    stringz(namestr);
                    filterstring(namestr, text, true, true, true, true, MAXNAMELEN);
                    if(!*namestr) copystring(namestr, "unnamed");
                    if(strcmp(d->name, namestr))
                    {
                        string oldname, newname;
                        copystring(oldname, game::colourname(d));
                        d->setinfo(namestr, c1, c2, model, vanity, mixer, lweaps, rweaps);
                        copystring(newname, game::colourname(d));
                        if(showpresence >= (waiting(false) ? 2 : 1) && !isignored(d->clientnum))
                            echomsg(colourmagenta, "%s is now known as %s", oldname, newname);
                    }
                    else d->setinfo(namestr, c1, c2, model, vanity, mixer, lweaps, rweaps);
                    d->checkpointspawn = cps;
                    break;
                }

                case N_CLIENTINIT: // cn colour model checkpoint team priv name vanity mixer count <loadweaps> count <randweaps> handle steamid hostip <version>
                {
                    int tcn = getint(p);
                    verinfo dummy;
                    gameent *d = game::newclient(tcn);
                    if(!d)
                    {
                        loopk(6) getint(p);
                        loopk(3) getstring(text, p);
                        loopj(2)
                        {
                            int w = getint(p);
                            loopk(w) getint(p);
                        }
                        loopk(3) getstring(text, p);
                        dummy.get(p);
                        break;
                    }
                    int c1 = getint(p), c2 = getint(p), model = getint(p), cps = getint(p), team = clamp(getint(p), int(T_NEUTRAL), int(T_MAX - 1)), priv = getint(p);
                    getstring(text, p);
                    stringz(namestr);
                    filterstring(namestr, text, true, true, true, true, MAXNAMELEN);
                    if(!*namestr) copystring(namestr, "unnamed");
                    stringz(vanity);
                    getstring(vanity, p);
                    stringz(mixer);
                    getstring(mixer, p);
                    int lw = getint(p);
                    vector<int> lweaps;
                    loopk(lw) lweaps.add(getint(p));
                    int rw = getint(p);
                    vector<int> rweaps;
                    loopk(rw) rweaps.add(getint(p));
                    getstring(d->handle, p);
                    getstring(d->steamid, p);
                    getstring(d->hostip, p);
                    if(d != game::player1) d->version.get(p);
                    else dummy.get(p);
                    d->checkpointspawn = cps;
                    d->team = team;
                    d->privilege = priv;
                    if(d->name[0]) d->setinfo(namestr, c1, c2, model, vanity, mixer, lweaps, rweaps); // already connected
                    else // new client
                    {
                        d->setinfo(namestr, c1, c2, model, vanity, mixer, lweaps, rweaps);
                        if(showpresence >= (waiting(false) ? 2 : 1))
                        {
                            int amt = otherclients(true);
                            stringz(ipaddr);
                            if(showpresencehostinfo && client::haspriv(game::player1, G(iphostlock))) formatstring(ipaddr, " (%s)", d->hostip);
                            if(priv > PRIV_NONE)
                            {
                                if(d->handle[0]) echomsg(colourgreen, "%s%s joined the game (\fs\fy%s\fS: \fs\fc%s\fS) [%d.%d.%d-%s%d-%s] (%d %s)", game::colourname(d), ipaddr, server::privname(d->privilege), d->handle, d->version.major, d->version.minor, d->version.patch, plat_name(d->version.platform), d->version.arch, d->version.branch, amt, amt != 1 ? "players" : "player");
                                else echomsg(colourgreen, "%s%s joined the game (\fs\fy%s\fS) [%d.%d.%d-%s%d-%s] (%d %s)", game::colourname(d), ipaddr, server::privname(d->privilege), d->version.major, d->version.minor, d->version.patch, plat_name(d->version.platform), d->version.arch, d->version.branch, amt, amt != 1 ? "players" : "player");
                            }
                            else echomsg(colourgreen, "%s%s joined the game [%d.%d.%d-%s%d-%s] (%d %s)", game::colourname(d), ipaddr, d->version.major, d->version.minor, d->version.patch, plat_name(d->version.platform), d->version.arch, d->version.branch, amt, amt != 1 ? "players" : "player");
                        }
                        if(needclipboard >= 0) needclipboard++;
                        game::specreset(d);
                    }
                    break;
                }

                case N_DISCONNECT:
                {
                    int lcn = getint(p), reason = getint(p);
                    game::clientdisconnected(lcn, reason);
                    break;
                }

                case N_LOADOUT:
                {
                    hud::showscores(false);
                    triggereventcallbacks(CMD_EVENT_GAME_LOADOUT);
                    lastplayerinfo = 0;
                    break;
                }

                case N_SPAWN:
                {
                    int lcn = getint(p);
                    gameent *f = game::newclient(lcn);
                    if(!f || f == game::player1 || f->ai) f = NULL;
                    parsestate(f, p);
                    break;
                }

                case N_SPAWNSTATE:
                {
                    int lcn = getint(p), ent = getint(p);
                    gameent *f = game::newclient(lcn);
                    if(!f || (f != game::player1 && !f->ai))
                    {
                        parsestate(NULL, p);
                        break;
                    }
                    if(f == game::player1 && editmode) toggleedit(true);
                    parsestate(f, p);
                    game::respawned(f, true, ent);
                    break;
                }

                case N_SHOTFX:
                {
                    int scn = getint(p), weap = getint(p), flags = getint(p), len = getint(p), target = getint(p);
                    vec from, dest;
                    loopk(3) from[k] = getint(p)/DMF;
                    loopk(3) dest[k] = getint(p)/DMF;
                    int ls = getint(p);
                    vector<shotmsg> shots;
                    loopj(ls)
                    {
                        shotmsg &s = shots.add();
                        s.id = getint(p);
                        loopk(3) s.pos[k] = getint(p);
                    }
                    gameent *t = game::getclient(scn), *v = game::getclient(target);
                    if(!t || !isweap(weap) || t == game::player1 || t->ai) break;
                    t->getseqid(SEQ_SHOT);
                    if(weap != t->weapselect) t->weapswitch(weap, lastmillis);
                    float scale = 1;
                    int sub = A(t->actortype, abilities)&(1<<A_A_AMMO) ? W2(weap, ammosub, WS(flags)) : 0;
                    if(W2(weap, cooktime, WS(flags)))
                    {
                        scale = len/float(W2(weap, cooktime, WS(flags)));
                        if(sub > 1) sub = int(ceilf(sub*scale));
                    }
                    projs::shootv(weap, flags, sub, 0, scale, from, dest, shots, t, false, v);
                    break;
                }

                case N_DESTROY:
                {
                    int scn = getint(p), targ = getint(p), idx = getint(p), num = idx < 0 ? 0-idx : idx;
                    gameent *t = game::getclient(scn);
                    loopi(num)
                    {
                        int id = getint(p);
                        if(t) projs::destruct(t, targ, id, idx < 0);
                    }
                    break;
                }

                case N_STICKY: // cn target id norm pos
                {
                    int scn = getint(p), tcn = getint(p), id = getint(p);
                    vec norm(0, 0, 0), pos(0, 0, 0);
                    loopk(3) norm[k] = getint(p)/DNF;
                    loopk(3) pos[k] = getint(p)/DMF;
                    gameent *t = game::getclient(scn), *v = tcn >= 0 ? game::getclient(tcn) : NULL;
                    if(t && (tcn < 0 || v)) projs::sticky(t, id, norm, pos, v);
                    break;
                }

                case N_DAMAGE:
                {
                    int tcn = getint(p), acn = getint(p), weap = getint(p), flags = getint(p),
                        fromweap = getint(p), fromflags = getint(p), damage = getint(p), health = getint(p);
                    vec dir, vel;
                    loopk(3) dir[k] = getint(p)/DNF;
                    loopk(3) vel[k] = getint(p)/DNF;
                    dir.safenormalize();
                    float dist = getint(p)/DNF;
                    gameent *m = game::getclient(tcn), *v = game::getclient(acn);
                    if(!m || !v) break;
                    game::damaged(weap, flags, fromweap, fromflags, damage, health, m, v, lastmillis, dir, vel, dist);
                    break;
                }

                case N_BURNRES:
                {
                    int cn = getint(p);
                    gameent *m = game::getclient(cn);
                    if(!m) break;
                    m->burntime = getint(p);
                    m->burndelay = getint(p);
                    m->burndamage = getint(p);
                    break;
                }

                case N_BLEEDRES:
                {
                    int cn = getint(p);
                    gameent *m = game::getclient(cn);
                    if(!m) break;
                    m->bleedtime = getint(p);
                    m->bleeddelay = getint(p);
                    m->bleeddamage = getint(p);
                    break;
                }

                case N_SHOCKRES:
                {
                    int cn = getint(p);
                    gameent *m = game::getclient(cn);
                    if(!m) break;
                    m->shocktime = getint(p);
                    m->shockdelay = getint(p);
                    m->shockdamage = getint(p);
                    m->shockstun = getint(p);
                    m->shockstunscale = getfloat(p);
                    m->shockstunfall = getfloat(p);
                    m->shockstuntime = getint(p);
                    break;
                }

                case N_CORRODERES:
                {
                    int cn = getint(p);
                    gameent *m = game::getclient(cn);
                    if(!m) break;
                    m->corrodetime = getint(p);
                    m->corrodedelay = getint(p);
                    m->corrodedamage = getint(p);
                    break;
                }

                case N_RELOAD:
                {
                    int trg = getint(p), weap = getint(p), amt = getint(p), ammo = getint(p), store = getint(p);
                    gameent *m = game::getclient(trg);
                    if(!m || !isweap(weap) || m == game::player1 || m->ai) break;
                    weapons::weapreload(m, weap, amt, ammo, store, false);
                    break;
                }

                case N_REGEN:
                {
                    int trg = getint(p), heal = getint(p), amt = getint(p);
                    gameent *f = game::getclient(trg);
                    if(!f) break;
                    if(!amt)
                    {
                        f->impulse[IM_METER] = 0;
                        f->resetresidual();
                    }
                    else if(!f->lastregen || lastmillis-f->lastregen >= 500)
                    {
                        if(f->health < f->gethealth(game::gamemode, game::mutators) && !f->lastregen)
                            emitsound(S_REGEN_BEGIN, game::getplayersoundpos(f), f);
                        else emitsound(amt >= 0 ? S_REGEN_BOOST : S_REGEN_DECAY, game::getplayersoundpos(f), f);
                    }
                    f->health = heal;
                    f->lastregen = lastmillis;
                    f->lastregenamt = amt;
                    break;
                }

                case N_DIED:
                {
                    int vcn = getint(p), deaths = getint(p), tdeaths = getint(p), acn = getint(p), frags = getint(p), tfrags = getint(p),
                        spree = getint(p), style = getint(p), weap = getint(p), flags = getint(p), fromweap = getint(p), fromflags = getint(p),
                        damage = getint(p), material = getint(p);
                    gameent *m = game::getclient(vcn), *v = game::getclient(acn);
                    static vector<gameent *> assist; assist.setsize(0);
                    int count = getint(p);
                    loopi(count)
                    {
                        int lcn = getint(p);
                        gameent *log = game::getclient(lcn);
                        if(log) assist.add(log);
                    }
                    if(!v || !m) break;
                    m->deaths = deaths;
                    m->totaldeaths = tdeaths;
                    v->frags = frags;
                    v->totalfrags = tfrags;
                    v->spree = spree;
                    game::killed(weap, flags, fromweap, fromflags, damage, m, v, assist, style, material);
                    m->lastdeath = lastmillis;
                    m->weapreset(true);
                    break;
                }

                case N_POINTS:
                {
                    int acn = getint(p), add = getint(p), points = getint(p), total = getint(p);
                    gameent *v = game::getclient(acn);
                    if(!v) break;
                    v->lastpoints = add;
                    v->points = points;
                    v->totalpoints = total;
                    break;
                }

                case N_WEAPDROP:
                {
                    int trg = getint(p), weap = getint(p), ds = getint(p);
                    gameent *m = game::getclient(trg);
                    bool local = m && (m == game::player1 || m->ai);
                    if(ds) loopj(ds)
                    {
                        int gs = getint(p), drop = getint(p), ammo = getint(p), target = getint(p);
                        if(m) projs::drop(m, gs, drop, ammo, local, weap, j, ds, game::getclient(target));
                    }
                    if(isweap(weap) && m)
                    {
                        if(m->weapswitch(weap, lastmillis, W(weap, delayswitch)))
                            emitsound(WSND(weap, S_W_SWITCH), weapons::getweapsoundpos(m, TAG_ORIGIN), m, &m->wschan[WS_MAIN_CHAN]);
                    }
                    break;
                }

                case N_WEAPSELECT:
                {
                    int trg = getint(p), weap = getint(p);
                    gameent *m = game::getclient(trg);
                    if(!m || !isweap(weap)) break;
                    weapons::weapselect(m, weap, (1<<W_S_SWITCH)|(1<<W_S_RELOAD), false);
                    break;
                }

                case N_WEAPCOOK:
                {
                    int trg = getint(p), weap = getint(p), etype = getint(p), offtime = getint(p);
                    gameent *m = game::getclient(trg);
                    if(!m || !isweap(weap)) break;
                    if(etype >= 0)
                    {
                        float maxscale = 1;
                        int sub = A(m->actortype, abilities)&(1<<A_A_AMMO) ? W2(weap, ammosub, etype >= 1) : 0;
                        if(sub > 1 && m->weapammo[weap][W_A_CLIP] < sub) maxscale = m->weapammo[weap][W_A_CLIP]/float(sub);
                        m->setweapstate(weap, etype >= 2 ? W_S_ZOOM : W_S_POWER, max(int(W2(weap, cooktime, etype >= 1)*maxscale), 1), lastmillis, offtime);
                    }
                    else m->setweapstate(weap, W_S_IDLE, 0, lastmillis, 0, true);
                    break;
                }

                case N_RESUME:
                {
                    for(;;)
                    {
                        int lcn = getint(p);
                        if(p.overread() || lcn < 0) break;
                        gameent *f = game::newclient(lcn);
                        if(!f) parsestate(NULL, p);
                        else if(parsestate(f, p, true))
                            addmsg(N_RESUME, "ri", f->clientnum);
                    }
                    break;
                }

                case N_ITEMSPAWN:
                {
                    int ent = getint(p), value = getint(p), delay = getint(p);
                    if(!entities::ents.inrange(ent)) break;
                    entities::setspawn(ent, value, delay);
                    ai::itemspawned(ent, value!=0);

                    break;
                }

                case N_TRIGGER:
                {
                    int ent = getint(p), st = getint(p);
                    entities::setspawn(ent, st);
                    break;
                }

                case N_ITEMACC:
                { // uses a specific drop so the client knows what to replace
                    int lcn = getint(p), fcn = getint(p), ent = getint(p), ammoamt = getint(p), spawn = getint(p),
                        weap = getint(p), drop = getint(p), ammo = getint(p), delay = getint(p);
                    gameent *m = game::getclient(lcn);
                    if(!m) break;
                    if(entities::ents.inrange(ent) && enttype[entities::ents[ent]->type].usetype == EU_ITEM)
                        entities::useeffects(m, fcn, ent, ammoamt, spawn, weap, drop, ammo, delay);
                    break;
                }

                case N_EDITVAR:
                {
                    int t = getint(p), flags = getint(p)&IDF_TX_MASK;
                    bool commit = true;
                    getstring(text, p);
                    ident *id = idents.access(text);
                    if(!d || !id || !(id->flags&IDF_MAP) || id->type != t) commit = false;
                    switch(t)
                    {
                        case ID_VAR:
                        {
                            int val = getint(p);
                            if(commit)
                            {
                                if(id->flags&IDF_HEX && uint(id->maxval) == 0xFFFFFFFFU)
                                {
                                    if(uint(val) > uint(id->maxval)) val = uint(id->maxval);
                                    else if(uint(val) < uint(id->minval)) val = uint(id->minval);
                                }
                                else if(val > id->maxval) val = id->maxval;
                                else if(val < id->minval) val = id->minval;
                                setvar(text, val, true);
                            }
                            break;
                        }
                        case ID_FVAR:
                        {
                            float val = getfloat(p);
                            if(commit)
                            {
                                if(val > id->maxvalf) val = id->maxvalf;
                                else if(val < id->minvalf) val = id->minvalf;
                                setfvar(text, val, true);
                            }
                            break;
                        }
                        case ID_SVAR:
                        {
                            int vlen = getint(p);
                            if(vlen < 0 || vlen > p.remaining()) break;
                            char *val = newstring(vlen);
                            getstring(val, p, vlen+1);
                            if(commit)
                            {
                                setsvar(text, val, true);
                            }
                            delete[] val;
                            break;
                        }
                        case ID_ALIAS:
                        {
                            int vlen = getint(p);
                            if(vlen < 0 || vlen > p.remaining()) break;
                            char *val = newstring(vlen);
                            getstring(val, p, vlen+1);
                            if(commit || !id) // set aliases anyway
                            {
                                if(flags&IDF_META) mapmeta(text, val);
                                else
                                {
                                    mapalias(text, val);
                                    echomsg(colouryellow, "%s set map alias \fs\fc%s\fS to \fs\fc%s\fS", game::colourname(d), text, val);
                                }
                            }
                            delete[] val;
                            break;
                        }
                        default: break;
                    }
                    break;
                }

                case N_CLIPBOARD:
                {
                    int tcn = getint(p), unpacklen = getint(p), packlen = getint(p);
                    gameent *d = game::getclient(tcn);
                    ucharbuf q = p.subbuf(max(packlen, 0));
                    if(d) unpackeditinfo(d->edit, q.buf, q.maxlen, unpacklen);
                    break;
                }

                case N_UNDO:
                case N_REDO:
                {
                    int cn = getint(p), unpacklen = getint(p), packlen = getint(p);
                    gameent *d = game::getclient(cn);
                    ucharbuf q = p.subbuf(max(packlen, 0));
                    if(d) unpackundo(q.buf, q.maxlen, unpacklen);
                    break;
                }

                case N_EDITF:            // coop editing messages
                case N_EDITT:
                case N_EDITM:
                case N_FLIP:
                case N_COPY:
                case N_PASTE:
                case N_ROTATE:
                case N_REPLACE:
                case N_DELCUBE:
                case N_EDITVSLOT:
                {
                    if(!d) return;
                    selinfo s;
                    s.o.x = getint(p); s.o.y = getint(p); s.o.z = getint(p);
                    s.s.x = getint(p); s.s.y = getint(p); s.s.z = getint(p);
                    s.grid = getint(p); s.orient = getint(p);
                    s.cx = getint(p); s.cxs = getint(p); s.cy = getint(p), s.cys = getint(p);
                    s.corner = getint(p);
                    switch(type)
                    {
                        case N_EDITF: { int dir = getint(p), mode = getint(p); if(s.validate()) mpeditface(dir, mode, s, false); break; }
                        case N_EDITT:
                        {
                            int tex = getint(p),
                                allfaces = getint(p);
                            if(p.remaining() < 2) return;
                            int extra = lilswap(*(const ushort *)p.pad(2));
                            if(p.remaining() < extra) return;
                            ucharbuf ebuf = p.subbuf(extra);
                            if(s.validate()) mpedittex(tex, allfaces, s, ebuf);
                            break;
                        }
                        case N_EDITM: { int mat = getint(p), filter = getint(p), style = getint(p); if(s.validate()) mpeditmat(mat, filter, style, s, false); break; }
                        case N_FLIP: if(s.validate()) mpflip(s, false); break;
                        case N_COPY: if(d && s.validate()) mpcopy(d->edit, s, false); break;
                        case N_PASTE: if(d && s.validate()) mppaste(d->edit, s, false); break;
                        case N_ROTATE: { int dir = getint(p); if(s.validate()) mprotate(dir, s, false); break; }
                        case N_REPLACE:
                        {
                            int oldtex = getint(p),
                                newtex = getint(p),
                                insel = getint(p);
                            if(p.remaining() < 2) return;
                            int extra = lilswap(*(const ushort *)p.pad(2));
                            if(p.remaining() < extra) return;
                            ucharbuf ebuf = p.subbuf(extra);
                            if(s.validate()) mpreplacetex(oldtex, newtex, insel>0, s, ebuf);
                            break;
                        }
                        case N_DELCUBE: if(s.validate()) mpdelcube(s, false); break;
                        case N_EDITVSLOT:
                        {
                            int delta = getint(p),
                                allfaces = getint(p);
                            if(p.remaining() < 2) return;
                            int extra = lilswap(*(const ushort *)p.pad(2));
                            if(p.remaining() < extra) return;
                            ucharbuf ebuf = p.subbuf(extra);
                            if(s.validate()) mpeditvslot(delta, allfaces, s, ebuf);
                            break;
                        }
                    }
                    break;
                }
                case N_REMIP:
                {
                    if(!d) return;
                    conoutf(colouryellow, "%s remipped", game::colourname(d));
                    mpremip(false);
                    break;
                }
                case N_CALCLIGHT:
                    if(!d) return;
                    conoutf(colouryellow, "%s calced lights", game::colourname(d));
                    mpcalclight(false);
                    break;
                case N_EDITENT:            // coop edit of ent
                {
                    if(!d) return;
                    int i = getint(p);
                    float x = getint(p)/DMF, y = getint(p)/DMF, z = getint(p)/DMF;
                    int type = getint(p), numattrs = getint(p);
                    attrvector attrs;
                    attrs.add(0, clamp(numattrs, entities::numattrs(type), MAXENTATTRS));
                    loopk(numattrs)
                    {
                        int val = getint(p);
                        if(attrs.inrange(k)) attrs[k] = val;
                    }
                    mpeditent(i, vec(x, y, z), type, attrs, false);
                    entities::setspawn(i, 0);
                    break;
                }

                case N_EDITLINK:
                {
                    if(!d) return;
                    int b = getint(p), index = getint(p), node = getint(p);
                    entities::linkents(index, node, b!=0, false, false);
                    break;
                }

                case N_PONG:
                    addmsg(N_CLIENTPING, "i", game::player1->ping = (game::player1->ping*5+totalmillis-getint(p))/6);
                    break;

                case N_CLIENTPING:
                    if(!d) return;
                    d->ping = getint(p);
                    loopv(game::players) if(game::players[i] && game::players[i]->ownernum == d->clientnum)
                        game::players[i]->ping = d->ping;
                    break;

                case N_MASTERMODE:
                {
                    int tcn = getint(p);
                    mastermode = getint(p);
                    if(tcn >= 0)
                    {
                        gameent *e = game::getclient(tcn);
                        if(e)
                        {
                            echomsg(colouryellow, "%s set \fs\fcmastermode\fS to \fs\fc%d\fS (\fs\fc%s\fS)", game::colourname(e), mastermode, mastermodename(mastermode));
                            break;
                        }
                    }
                    echomsg(colouryellow, "The server set \fs\fcmastermode\fS to \fs\fc%d\fS (\fs\fc%s\fS)", mastermode, mastermodename(mastermode));
                    break;
                }

                case N_TICK:
                {
                    int state = getint(p), remain = getint(p), elapsed = getint(p), wait = getint(p);
                    game::timeupdate(state, remain, elapsed, wait);
                    break;
                }

                case N_SERVMSG:
                {
                    int color = getint(p);
                    getstring(text, p);
                    conoutf(color, "%s", text);
                    break;
                }

                case N_SENDDEMOLIST:
                {
                    int demos = getint(p);
                    if(demos <= 0) conoutf(colourorange, "No demos available");
                    else loopi(demos)
                    {
                        getstring(text, p);
                        int len = getint(p), ctime = getint(p);
                        if(p.overread()) break;
                        conoutf(colouryellow, "Demo: %2d. \fs\fc%s\fS recorded \fs\fc%s UTC\fS [\fs\fw%.2f%s\fS]", i+1, text, gettime(ctime, "%Y-%m-%d %H:%M.%S"), len > 1024*1024 ? len/(1024*1024.f) : len/1024.0f, len > 1024*1024 ? "MB" : "kB");
                    }
                    break;
                }

                case N_DEMOPLAYBACK:
                {
                    bool wasdemopb = demoplayback;
                    demoplayback = getint(p)!=0;
                    if(demoplayback) game::player1->state = CS_SPECTATOR;
                    else loopv(game::players) if(game::players[i]) game::clientdisconnected(i);
                    game::player1->clientnum = getint(p);
                    if(!demoplayback && wasdemopb && demoendless)
                    {
                        stringz(demofile);
                        if(*demolist)
                        {
                            int r = rnd(listlen(demolist)), len = 0;
                            const char *elem = indexlist(demolist, r, len);
                            if(len > 0) copystring(demofile, elem, len+1);
                        }
                        if(!*demofile)
                        {
                            vector<char *> files;
                            listfiles("demos", "dmo", files);
                            while(!files.empty())
                            {
                                int r = rnd(files.length());
                                if(files[r][0] != '.')
                                {
                                    copystring(demofile, files[r]);
                                    break;
                                }
                                else
                                {
                                    delete[] files[r];
                                    files.remove(r);
                                }
                            }
                            files.deletearrays();
                        }
                        if(*demofile) addmsg(N_MAPVOTE, "rsi2", demofile, G_DEMO, 0);
                    }
                    break;
                }

                case N_DEMOREADY:
                {
                    int num = getint(p), ctime = getint(p), len = getint(p);
                    getstring(text, p);
                    conoutf(colouryellow, "Demo \fs\fc%s\fS recorded \fs\fc%s UTC\fS [\fs\fw%.2f%s\fS]", text, gettime(ctime, "%Y-%m-%d %H:%M.%S"), len > 1024*1024 ? len/(1024*1024.f) : len/1024.0f, len > 1024*1024 ? "MB" : "kB");
                    if(demoautoclientsave) getdemo(num, "");
                    break;
                }

                case N_CURRENTPRIV:
                {
                    int mn = getint(p), priv = getint(p);
                    getstring(text, p);
                    gameent *m = game::getclient(mn);
                    if(m)
                    {
                        m->privilege = priv;
                        copystring(m->handle, text);
                    }
                    break;
                }

                case N_EDITMODE:
                {
                    int val = getint(p);
                    if(!d) break;
                    if(val) d->state = CS_EDITING;
                    else
                    {
                        d->state = CS_ALIVE;
                        d->editspawn(game::gamemode, game::mutators, d->gethealth(game::gamemode, game::mutators));
                    }
                    d->resetinterp();
                    projs::removeplayer(d);
                    break;
                }

                case N_SPECTATOR:
                {
                    int sn = getint(p), val = getint(p);
                    gameent *s = game::newclient(sn);
                    if(!s) break;

                    if(s == game::player1)
                    {
                        if(editmode) toggleedit(true);
                        game::specreset();
                        if(m_speedrun(game::gamemode)) game::specmode = 0;
                    }

                    if(val)
                    {
                        s->state = CS_SPECTATOR;
                        s->quarantine = val == 2;
                        s->respawned = -1;
                    }
                    else
                    {
                        if(s->state == CS_SPECTATOR)
                        {
                            s->state = CS_WAITING;
                            if(s != game::player1 && !s->ai) s->resetinterp();
                            game::waiting.removeobj(s);
                        }

                        s->quarantine = false;
                    }
                    break;
                }

                case N_WAITING:
                {
                    int sn = getint(p);
                    gameent *s = game::newclient(sn);
                    if(!s) break;
                    if(s == game::player1)
                    {
                        if(editmode) toggleedit(true);
                        hud::showscores(false);
                        s->stopmoving(true);
                        game::waiting.setsize(0);
                        gameent *d;
                        loopv(game::players) if((d = game::players[i]) && d->actortype == A_PLAYER && d->state == CS_WAITING)
                            game::waiting.add(d);
                    }
                    else if(!s->ai) s->resetinterp();
                    game::waiting.removeobj(s);
                    if(s->state == CS_ALIVE) s->lastdeath = lastmillis; // so spawn delay shows properly
                    else entities::spawnplayer(s); // so they're not nowhere
                    s->state = CS_WAITING;
                    s->quarantine = false;
                    s->weapreset(true);
                    break;
                }

                case N_SETTEAM:
                {
                    int wn = getint(p), tn = getint(p);
                    gameent *w = game::getclient(wn);
                    if(!w) return;
                    if(w->team != tn)
                    {
                        if(m_team(game::gamemode, game::mutators) && w->actortype == A_PLAYER && showteamchange >= (w->team != T_NEUTRAL && tn != T_NEUTRAL ? 1 : 2))
                            echomsg(colourgrey, "%s is now on team %s", game::colourname(w), game::colourteam(tn));
                        w->team = tn;
                    }
                    break;
                }

                case N_INFOAFFIN:
                {
                    int flag = getint(p), converted = getint(p), owner = getint(p), owners = getint(p), enemy = getint(p), enemies = getint(p), points = getint(p);
                    if(m_defend(game::gamemode)) defend::updateaffinity(flag, owner, owners, enemy, enemies, converted, points);
                    break;
                }

                case N_SETUPAFFIN:
                {
                    if(m_defend(game::gamemode)) defend::parseaffinity(p);
                    break;
                }

                case N_MAPVOTE:
                {
                    int vn = getint(p);
                    gameent *v = game::getclient(vn);
                    getstring(text, p);
                    filterstring(text, text);
                    int reqmode = getint(p), reqmuts = getint(p);
                    if(!v) break;
                    vote(v, text, reqmode, reqmuts);
                    break;
                }

                case N_CLEARVOTE:
                {
                    int vn = getint(p);
                    gameent *v = game::getclient(vn);
                    if(!v) break;
                    clearvotes(v, true);
                    break;
                }

                case N_CHECKPOINT:
                {
                    int tn = getint(p), ent = getint(p);
                    gameent *t = game::getclient(tn);
                    if(!t || !m_speedrun(game::gamemode) || !entities::ents.inrange(ent) || entities::ents[ent]->type != CHECKPOINT)
                    {
                        if(ent < 0)
                        {
                            t->resetcheckpoint();
                            t->cpmillis = ent == -2 ? lastmillis : 0;
                            break;
                        }
                        if(getint(p) < 0) break;
                        loopi(2) getint(p);
                        break;
                    }

                    int laptime = getint(p);
                    if(laptime >= 0)
                    {
                        t->cplast = laptime;
                        t->cptime = getint(p);
                        t->points = getint(p);
                        t->clearimpulse();
                        t->resetcheckpoint();
                        if(showlaptimes >= (t != game::focus ? (t->actortype > A_PLAYER ? 3 : 2) : 1))
                        {
                            defformatstring(best, "%s", timestr(t->cptime, 1));
                            conoutf(colourwhite, "%s completed in \fs\fg%s\fS (best: \fs\fy%s\fS, laps: \fs\fc%d\fS)", game::colourname(t), timestr(t->cplast, 1), best, t->points);
                        }
                    }
                    else
                    {
                        if(t != game::player1 && !t->ai) t->setcheckpoint(ent, lastmillis, entities::ents[ent]->attrs[6]);
                        t->clearimpulse(impulsecheckpointclear, impulsecheckpointreset);
                    }

                    if((checkpointannounce&(t != game::focus ? 2 : 1) || (m_sr_gauntlet(game::gamemode, game::mutators) && checkpointannounce&4)) && checkpointannouncefilter&(1<<entities::ents[ent]->attrs[6]))
                    {
                        gamelog *log = new gamelog(GAMELOG_EVENT);
                        log->addlist("args", "type", "speedrun");
                        log->addlist("args", "flags", GAMELOG_F_BROADCAST);
                        switch(entities::ents[ent]->attrs[6])
                        {
                            case CP_START:
                                log->addlist("args", "action", "start");
                                break;

                            case CP_FINISH:
                            case CP_LAST:
                                log->addlist("args", "action", "finish");
                                break;

                            default:
                                log->addlist("args", "action", "checkpoint");
                                break;
                        }
                        log->addclient("client", t);
                        if(!log->push()) DELETEP(log);
                    }
                    entities::execlink(t, ent, false);
                }

                case N_SCORE:
                {
                    int team = getint(p), total = getint(p);
                    if(m_team(game::gamemode, game::mutators))
                    {
                        score &ts = hud::teamscore(team);
                        ts.total = total;
                    }
                    break;
                }

                case N_INITAFFIN:
                {
                    if(m_capture(game::gamemode)) capture::parseaffinity(p);
                    else if(m_bomber(game::gamemode)) bomber::parseaffinity(p);
                    break;
                }

                case N_DROPAFFIN:
                {
                    int ocn = getint(p), otc = getint(p), flag = getint(p);
                    vec droploc, inertia;
                    loopk(3) droploc[k] = getint(p)/DMF;
                    loopk(3) inertia[k] = getint(p)/DMF;
                    gameent *o = game::newclient(ocn);
                    if(o)
                    {
                        if(m_capture(game::gamemode)) capture::dropaffinity(o, flag, droploc, inertia, otc);
                        else if(m_bomber(game::gamemode)) bomber::dropaffinity(o, flag, droploc, inertia, otc);
                    }
                    break;
                }

                case N_SCOREAFFIN:
                {
                    int ocn = getint(p), relayflag = getint(p), goalflag = getint(p), score = getint(p);
                    gameent *o = game::newclient(ocn);
                    if(o)
                    {
                        if(m_capture(game::gamemode)) capture::scoreaffinity(o, relayflag, goalflag, score);
                        else if(m_bomber(game::gamemode)) bomber::scoreaffinity(o, relayflag, goalflag, score);
                    }
                    break;
                }

                case N_RETURNAFFIN:
                {
                    int ocn = getint(p), flag = getint(p);
                    gameent *o = game::newclient(ocn);
                    if(o && m_capture(game::gamemode)) capture::returnaffinity(o, flag);
                    break;
                }

                case N_TAKEAFFIN:
                {
                    int ocn = getint(p), flag = getint(p);
                    gameent *o = game::newclient(ocn);
                    if(o)
                    {
                        o->lastaffinity = lastmillis;
                        if(m_capture(game::gamemode)) capture::takeaffinity(o, flag);
                        else if(m_bomber(game::gamemode)) bomber::takeaffinity(o, flag);
                    }
                    break;
                }

                case N_RESETAFFIN:
                {
                    int flag = getint(p), value = getint(p);
                    vec resetpos(-1, -1, -1);
                    if(value == 2) loopk(3) resetpos[k] = getint(p)/DMF;
                    if(m_capture(game::gamemode)) capture::resetaffinity(flag, value, resetpos);
                    else if(m_bomber(game::gamemode)) bomber::resetaffinity(flag, value, resetpos);
                    break;
                }

                case N_GETMAP:
                {
                    conoutf(colouryellow, "Server has requested we send the map..");
                    if(!needsmap && !gettingmap) sendmap();
                    else addmsg(N_GETMAP, "r");
                    break;
                }

                case N_SENDMAP:
                {
                    conoutf(colouryellow, "Map data has been uploaded to the server");
                    break;
                }

                case N_FAILMAP:
                {
                    conoutf(colouryellow, "The server failed to get a map, sending them ours..");
                    sendmap();
                    needsmap = gettingmap = 0;
                    break;
                }

                case N_NEWMAP:
                {
                    int size = getint(p);
                    getstring(text, p);
                    if(size >= 0) emptymap(size, true, text);
                    else enlargemap(size == -2, true);
                    mapvotes.shrink(0);
                    needsmap = 0;
                    if(d)
                    {
                        int newsize = 0;
                        while(1<<newsize < worldsize) newsize++;
                        conoutf(colouryellow, size >= 0 ? "%s started new map \fs\fc%s\fS of size \fs\fc%d\fS" : "%s enlarged the map \fs\fc%s\fS to size \fs\fc%d\fS", game::colourname(d), mapname, newsize);
                    }
                    break;
                }

                case N_INITAI:
                {
                    int bn = getint(p), on = getint(p), at = getint(p), et = getint(p), sk = clamp(getint(p), 1, 101);
                    getstring(text, p);
                    int tm = getint(p), c1 = getint(p), c2 = getint(p), md = getint(p);
                    string vanity;
                    getstring(vanity, p);
                    string mixer;
                    getstring(mixer, p);
                    int lw = getint(p);
                    vector<int> lweaps;
                    loopk(lw) lweaps.add(getint(p));
                    gameent *b = game::newclient(bn);
                    if(!b) break;
                    ai::init(b, at, et, on, sk, bn, text, tm, c1, c2, md, vanity, mixer, lweaps);
                    break;
                }

                case N_AUTHCHAL:
                {
                    uint id = (uint)getint(p);
                    getstring(text, p);
                    vector<char> buf;
                    if(accountname[0] && accountpass[0] && answerchallenge(accountpass, text, buf))
                    {
                        conoutf(colourwhite, "Identifying as: \fs\fc%s\fS (\fs\fy%u\fS)", accountname, id);
                        addmsg(N_AUTHANS, "ris", id, buf.getbuf());
                    }
                    break;
                }

                case N_STEAMCHAL:
                {
                    char token[1024];
                    uint tokenlen;
                    if(cdpi::steam::clientauthticket(token, &tokenlen, connpeer ? &connpeer->address: NULL))
                    {
                        packetbuf sc(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
                        putint(sc, N_STEAMANS);
                        sendstring(cdpi::steam::steamuserid, sc);
                        putuint(sc, tokenlen);
                        if(tokenlen > 0) sc.put((uchar *)token, tokenlen);
                        sendclientpacket(sc.finalize(), 1);
                        conoutf(colouryellow, "Performing Steam ID challenge..");
                    }
                    else
                    {
                        conoutf(colourred, "Failed to perform Steam ID challenge");
                        addmsg(N_STEAMFAIL, "r");
                    }
                    break;
                }

                case N_QUEUEPOS:
                {
                    int qcn = getint(p), pos = getint(p);
                    gameent *o = game::newclient(qcn);
                    bool changed = o->queuepos != pos;
                    o->queuepos = pos;
                    if(o == game::focus && changed && o->state != CS_ALIVE)
                    {
                        if(o->queuepos < 0) echomsg(colouryellow, "You are now \fs\fzgcqueued\fS for the \fs\fcnext match\fS");
                        else if(o->queuepos) echomsg(colouryellow, "You are \fs\fzgc#%d\fS in the \fs\fcqueue\fS", o->queuepos+1);
                        else echomsg(colouryellow, "You are \fs\fzgcNEXT\fS in the \fs\fcqueue\fS");
                    }
                    break;
                }

                default:
                {
                    defformatstring(err, "type (got: %d, prev: %d, sender: %s)", type, prevtype, d ? game::colourname(d) : "<none>");
                    neterr(err);
                    return;
                }
            }
        }
    }

    int serverstat(serverinfo *a)
    {
        if(a->attr.length() > 4 && a->numplayers >= a->attr[4])
        {
            return SSTAT_FULL;
        }
        else if(a->attr.length() > 5) switch(a->attr[5])
        {
            case MASTERMODE_LOCKED:
            {
                return SSTAT_LOCKED;
            }
            case MASTERMODE_PRIVATE:
            case MASTERMODE_PASSWORD:
            {
                return SSTAT_PRIVATE;
            }
            default:
            {
                return SSTAT_OPEN;
            }
        }
        return SSTAT_UNKNOWN;
    }

    int servercompare(serverinfo *a, serverinfo *b)
    {
        int ac = 0, bc = 0;
        if(a->address.host == ENET_HOST_ANY || a->ping >= serverinfo::WAITING || a->attr.empty()) ac = -1;
        else ac = a->attr[0] == VERSION_GAME ? 0x7FFF : clamp(a->attr[0], 0, 0x7FFF-1);
        if(b->address.host == ENET_HOST_ANY || b->ping >= serverinfo::WAITING || b->attr.empty()) bc = -1;
        else bc = b->attr[0] == VERSION_GAME ? 0x7FFF : clamp(b->attr[0], 0, 0x7FFF-1);
        if(ac > bc) return -1;
        if(ac < bc) return 1;

        #define retcp(c) { int cv = (c); if(cv) { return reverse ? -cv : cv; } }
        #define retsw(c,d,e) { \
            int cv = (c), dv = (d); \
            if(cv != dv) \
            { \
                if((e) != reverse ? cv < dv : cv > dv) return -1; \
                if((e) != reverse ? cv > dv : cv < dv) return 1; \
            } \
        }

        if(serversortstyles.empty()) updateserversort();
        loopv(serversortstyles)
        {
            int style = serversortstyles[i];
            serverinfo *aa = a, *ab = b;
            bool reverse = false;
            if(style < 0)
            {
                style = 0-style;
                reverse = true;
            }

            switch(style)
            {
                case SINFO_STATUS:
                {
                    retsw(serverstat(aa), serverstat(ab), true);
                    break;
                }
                case SINFO_DESC:
                {
                    retcp(strcmp(aa->sdesc, ab->sdesc));
                    break;
                }
                case SINFO_MODE:
                {
                    if(aa->attr.length() > 1) ac = aa->attr[1];
                    else ac = 0;

                    if(ab->attr.length() > 1) bc = ab->attr[1];
                    else bc = 0;

                    retsw(ac, bc, true);
                }
                case SINFO_MUTS:
                {
                    if(aa->attr.length() > 2) ac = aa->attr[2];
                    else ac = 0;

                    if(ab->attr.length() > 2) bc = ab->attr[2];
                    else bc = 0;

                    retsw(ac, bc, true);
                    break;
                }
                case SINFO_MAP:
                {
                    retcp(strcmp(aa->map, ab->map));
                    break;
                }
                case SINFO_TIME:
                {
                    if(aa->attr.length() > 3) ac = aa->attr[3];
                    else ac = 0;

                    if(ab->attr.length() > 3) bc = ab->attr[3];
                    else bc = 0;

                    retsw(ac, bc, false);
                    break;
                }
                case SINFO_NUMPLRS:
                {
                    retsw(aa->numplayers, ab->numplayers, false);
                    break;
                }
                case SINFO_MAXPLRS:
                {
                    if(aa->attr.length() > 4) ac = aa->attr[4];
                    else ac = 0;

                    if(ab->attr.length() > 4) bc = ab->attr[4];
                    else bc = 0;

                    retsw(ac, bc, false);
                    break;
                }
                case SINFO_PING:
                {
                    retsw(aa->ping, ab->ping, true);
                    break;
                }
                case SINFO_PRIO:
                {
                    retsw(aa->priority, ab->priority, false);
                    break;
                }
                default: break;
            }
        }
        return strcmp(a->name, b->name);
    }

    void parsepacketclient(int chan, packetbuf &p)  // processes any updates from the server
    {
        if(p.packet->flags&ENET_PACKET_FLAG_UNSEQUENCED) return;
        switch(chan)
        {
            case 0:
                parsepositions(p);
                break;

            case 1:
                parsemessages(-1, NULL, p);
                break;

            case 2:
                receivefile(p.buf, p.maxlen);
                break;
        }
    }

    void getservers(int server, int prop, int idx)
    {
        if(server < 0) intret(servers.length());
        else if(servers.inrange(server))
        {
            serverinfo *si = servers[server];
            if(prop < 0) intret(4);
            else switch(prop)
            {
                case 0:
                    if(idx < 0) intret(11);
                    else switch(idx)
                    {
                        case 0: intret(serverstat(si)); break;
                        case 1: result(si->name); break;
                        case 2: intret(si->port); break;
                        case 3: result(si->sdesc[0] ? si->sdesc : si->name); break;
                        case 4: result(si->map); break;
                        case 5: intret(si->numplayers); break;
                        case 6: intret(si->ping); break;
                        case 7: intret(si->lastinfo); break;
                        case 8: result(si->authhandle); break;
                        case 9: result(si->flags); break;
                        case 10: result(si->branch); break;
                        case 11: intret(si->priority); break;
                    }
                    break;
                case 1:
                    if(idx < 0) intret(si->attr.length());
                    else if(si->attr.inrange(idx)) intret(si->attr[idx]);
                    break;
                case 2:
                    if(idx < 0) intret(si->players.length());
                    else if(si->players.inrange(idx)) result(si->players[idx]);
                    break;
                case 3:
                    if(idx < 0) intret(si->handles.length());
                    else if(si->handles.inrange(idx)) result(si->handles[idx]);
                    break;
            }
        }
    }
    ICOMMAND(0, getserver, "bbb", (int *server, int *prop, int *idx), getservers(*server, *prop, *idx));

    #define GETSERVER(name, test, incr) \
        ICOMMAND(0, getserver##name, "", (), \
        { \
            int n = 0; \
            loopv(servers) if(test) n += incr; \
            intret(n); \
        }); \
        ICOMMAND(0, getserver##name##if, "re", (ident *id, uint *cond), \
        { \
            int n = 0; \
            loopstart(id, stack); \
            loopv(servers) \
            { \
                loopiter(id, stack, i); \
                if(test && executebool(cond)) n += incr; \
            } \
            loopend(id, stack); \
            intret(n); \
        });

    GETSERVER(count, true, 1);
    GETSERVER(active, servers[i]->numplayers, 1);
    GETSERVER(players, servers[i]->numplayers, servers[i]->numplayers);

    void loopserver(ident *id, uint *cond, uint *body, uint *none)
    {
        loopstart(id, stack);
        if(servers.empty())
        {
            loopiter(id, stack, -1);
            execute(none);
        }
        else loopv(servers)
        {
            loopiter(id, stack, i);
            if(cond && !executebool(cond)) continue;
            execute(body);
        }
        loopend(id, stack);
    }

    ICOMMAND(0, loopservers, "ree", (ident *id, uint *body, uint *none), loopserver(id, NULL, body, none));
    ICOMMAND(0, loopserversif, "reee", (ident *id, uint *cond, uint *body, uint *none), loopserver(id, cond, body, none));

    void completeplayers(const char **nextcomplete, const char *start, int commandsize, const char *lastcomplete, bool reverse)
    {
        static vector<char *> names;
        if(!names.empty()) names.deletearrays();
        gameent *d = NULL;
        int size = completesize-commandsize;
        const char *name = game::colourname(game::player1, NULL, false, true, 0);
        if(!size || !strncmp(name, &start[commandsize], size))
            names.add(newstring(name));
        loopv(game::players) if((d = game::players[i]) != NULL)
        {
            name = game::colourname(d, NULL, false, true, 0);
            if(!size || !strncmp(name, &start[commandsize], size))
                names.add(newstring(name));
        }
        loopv(names)
        {
            if(strcmp(names[i], lastcomplete)*(reverse ? -1 : 1) > 0 && (!*nextcomplete || strcmp(names[i], *nextcomplete)*(reverse ? -1 : 1) < 0))
                *nextcomplete = names[i];
        }
    }

    #define LOOPCLIENTS(name,op,lp,nop) \
        ICOMMAND(0, loopclients##name, "iire", (int *count, int *skip, ident *id, uint *body), \
        { \
            loopstart(id, stack); \
            int amt = 1; \
            loopv(game::players) if(game::players[i]) amt++; \
            op(amt, *count, *skip, \
            { \
                int r = -1; \
                int n = nop ? amt-1 : 0; \
                if(!i) \
                { \
                    if(nop ? n <= i : n >= i) r = game::player1->clientnum; \
                    if(nop) n--; \
                    else n++; \
                } \
                else \
                { \
                    lp(game::players) if(game::players[k]) \
                    { \
                        if(nop ? n <= i : n >= i) \
                        { \
                            r = game::players[k]->clientnum; \
                            break; \
                        } \
                        if(nop) n--; \
                        else n++; \
                    } \
                } \
                if(r >= 0) \
                { \
                    loopiter(id, stack, r); \
                    execute(body); \
                } \
            }); \
            loopend(id, stack); \
        }); \
        ICOMMAND(0, loopclients##name##if, "iiree", (int *count, int *skip, ident *id, uint *cond, uint *body), \
        { \
            loopstart(id, stack); \
            int amt = 1; \
            loopv(game::players) if(game::players[i]) amt++; \
            op(amt, *count, *skip, \
            { \
                int r = -1; \
                int n = nop ? amt-1 : 0; \
                if(!i) \
                { \
                    if(nop ? n <= i : n >= i) r = game::player1->clientnum; \
                    if(nop) n--; \
                    else n++; \
                } \
                else \
                { \
                    lp(game::players) if(game::players[k]) \
                    { \
                        if(nop ? n <= i : n >= i) \
                        { \
                            r = game::players[k]->clientnum; \
                            break; \
                        } \
                        if(nop) n--; \
                        else n++; \
                    } \
                } \
                if(r >= 0) \
                { \
                    loopiter(id, stack, r); \
                    if(executebool(cond)) execute(body); \
                } \
            }); \
            loopend(id, stack); \
        }); \
        ICOMMAND(0, loopclients##name##while, "iiree", (int *count, int *skip, ident *id, uint *cond, uint *body), \
        { \
            loopstart(id, stack); \
            int amt = 1; \
            loopv(game::players) if(game::players[i]) amt++; \
            op(amt, *count, *skip, \
            { \
                int r = -1; \
                int n = nop ? amt-1 : 0; \
                if(!i) \
                { \
                    if(nop ? n <= i : n >= i) r = game::player1->clientnum; \
                    if(nop) n--; \
                    else n++; \
                } \
                else \
                { \
                    lp(game::players) if(game::players[k]) \
                    { \
                        if(nop ? n <= i : n >= i) \
                        { \
                            r = game::players[k]->clientnum; \
                            break; \
                        } \
                        if(nop) n--; \
                        else n++; \
                    } \
                } \
                if(r >= 0) \
                { \
                    loopiter(id, stack, r); \
                    if(!executebool(cond)) break; \
                    execute(body); \
                } \
            }); \
            loopend(id, stack); \
        });
    LOOPCLIENTS(,loopcsi,loopvk,false);
    LOOPCLIENTS(rev,loopcsirev,loopvkrev,true);

    #define LOOPINVENTORY(name,op,lp,nop) \
        ICOMMAND(IDF_NAMECOMPLETE, loopinventory##name, "siire", (char *who, int *count, int *skip, ident *id, uint *body), \
        { \
            gameent *d = game::getclient(parseplayer(who)); \
            if(!d) return; \
            loopstart(id, stack); \
            int amt = d->holdweapcount(m_weapon(d->actortype, game::gamemode, game::mutators), lastmillis); \
            op(amt, *count, *skip, \
            { \
                int r = -1; \
                int n = nop ? amt-1 : 0; \
                lp(W_ALL) if(d->holdweap(k, m_weapon(d->actortype, game::gamemode, game::mutators), lastmillis)) \
                { \
                    if(nop ? n <= i : n >= i) \
                    { \
                        r = k; \
                        break; \
                    } \
                    if(nop) n--; \
                    else n++; \
                } \
                if(r >= 0) \
                { \
                    loopiter(id, stack, r); \
                    execute(body); \
                } \
            }); \
            loopend(id, stack); \
        });
    LOOPINVENTORY(,loopcsi,loopk,false);
    LOOPINVENTORY(rev,loopcsirev,loopkrev,true);

    CLCOMMAND(lastactitem, intret(d->lastactitem));
    CLCOMMANDM(actitem, "sb", (char *who, int *n), intret(*n >= 0 ? (d->actitems.inrange(*n) ? 1 : 0) : d->actitems.length()));
    CLCOMMANDM(actitemtype, "sb", (char *who, int *n), intret(d->actitems.inrange(*n) ? d->actitems[*n].type : -1));
    CLCOMMANDM(actitement, "sb", (char *who, int *n), intret(d->actitems.inrange(*n) ? d->actitems[*n].ent : -1));
    CLCOMMANDM(actitemid, "sb", (char *who, int *n), intret(d->actitems.inrange(*n) ? d->actitems[*n].id : -1));
    CLCOMMANDM(actitemscore, "sb", (char *who, int *n), floatret(d->actitems.inrange(*n) ? d->actitems[*n].score : 0.f));
    CLCOMMANDM(actitemmillis, "sb", (char *who, int *n), intret(d->actitems.inrange(*n) ? d->actitems[*n].millis : 0));
    CLCOMMANDM(actitementer, "sb", (char *who, int *n), intret(d->actitems.inrange(*n) ? d->actitems[*n].enter : -1));
    CLCOMMANDM(actitemleave, "sb", (char *who, int *n), intret(d->actitems.inrange(*n) ? d->actitems[*n].leave : -1));
    CLCOMMANDM(actitemready, "sb", (char *who, int *n), intret(d->actitems.inrange(*n) ? (d->actitems[*n].millis == d->lastactitem ? 1 : 0) : 0));
    CLCOMMANDM(actitemidx, "sbbb", (char *who, int *n, int *v, int *r),
    {
        loopv(d->actitems)
        {
            if(*n >= 0 && d->actitems[i].type != *n) continue;
            if(*v >= 0 && d->actitems[i].ent != *v) continue;
            if(*r >= 0 && d->actitems[i].id != *r) continue;

            intret(i);

            return;
        }

        intret(-1);
    });

    #define LOOPACTITEMS(name, op) \
        ICOMMAND(IDF_NAMECOMPLETE, loopactitems##name, "siire", (char *who, int *count, int *skip, ident *id, uint *body), \
        { \
            gameent *d = game::getclient(parseplayer(who)); \
            if(!d) return; \
            loopstart(id, stack); \
            op(d->actitems, *count, *skip, \
            { \
                loopiter(id, stack, i); \
                execute(body); \
            }); \
            loopend(id, stack); \
        }); \
        ICOMMAND(IDF_NAMECOMPLETE, loopactitems##name##if, "siiree", (char *who, int *count, int *skip, ident *id, uint *cond, uint *body), \
        { \
            gameent *d = game::getclient(parseplayer(who)); \
            if(!d) return; \
            loopstart(id, stack); \
            op(d->actitems, *count, *skip, \
            { \
                loopiter(id, stack, i); \
                if(executebool(cond)) execute(body); \
            }); \
            loopend(id, stack); \
        }); \
        ICOMMAND(IDF_NAMECOMPLETE, loopactitems##name##while, "siiree", (char *who, int *count, int *skip, ident *id, uint *cond, uint *body), \
        { \
            gameent *d = game::getclient(parseplayer(who)); \
            if(!d) return; \
            loopstart(id, stack); \
            op(d->actitems, *count, *skip, \
            { \
                loopiter(id, stack, i); \
                if(!executebool(cond)) break; \
                execute(body); \
            }); \
            loopend(id, stack); \
        });
    LOOPACTITEMS(,loopcsv);
    LOOPACTITEMS(rev,loopcsvrev);

    #define LOOPREADYITEMS(name, op) \
        ICOMMAND(IDF_NAMECOMPLETE, loopreadyitems##name, "siire", (char *who, int *count, int *skip, ident *id, uint *body), \
        { \
            gameent *d = game::getclient(parseplayer(who)); \
            if(!d) return; \
            loopstart(id, stack); \
            op(d->actitems, *count, *skip, \
            { \
                if(d->actitems[i].millis != d->lastactitem) break; \
                loopiter(id, stack, i); \
                execute(body); \
            }); \
            loopend(id, stack); \
        });
    LOOPREADYITEMS(,loopcsv);
    LOOPREADYITEMS(rev,loopcsvrev);

    CLCOMMANDK(presence, intret(1), intret(0));
    CLCOMMAND(yaw, floatret(d->yaw));
    CLCOMMAND(pitch, floatret(d->pitch));
    CLCOMMAND(roll, floatret(d->roll));
    CLCOMMAND(opacity, floatret(game::opacity(d)));

    CLCOMMANDM(radarallow, "sb", (char *who, int *self),
    {
        vec dir(0, 0, 0);
        float dist = -1;
        intret(radarallow(camera1->o, d, dir, dist, *self >= 0 ? *self != 0 : false) ? 1 : 0);
    });

    CLCOMMAND(radardist,
    {
        vec dir(0, 0, 0);
        float dist = -1;
        if(!radarallow(camera1->o, d, dir, dist)) return;
        floatret(dist);
    });

    CLCOMMAND(radardir,
    {
        vec dir(0, 0, 0);
        float dist = -1;
        if(!radarallow(camera1->o, d, dir, dist)) return;
        dir.rotate_around_z(-camera1->yaw*RAD).normalize();
        floatret(-atan2(dir.x, dir.y)/RAD);
    });

    CLCOMMAND(radaryaw,
    {
        vec dir(0, 0, 0);
        float dist = -1;
        if(!radarallow(camera1->o, d, dir, dist)) return;
        floatret(d->yaw-camera1->yaw);
    });

    CLCOMMAND(radardirxy,
    {
        vec dir(0, 0, 0);
        float dist = -1;
        if(!radarallow(camera1->o, d, dir, dist)) return;
        dir.rotate_around_z(-camera1->yaw*RAD).normalize();
        defformatstring(output, "%s %s", floatstr(dir.x), floatstr(dir.y));
        result(output);
    });

    CLCOMMANDM(name, "sbib", (char *who, int *colour, int *icon, int *dupname), result(game::colourname(d, NULL, *icon!=0, *dupname!=0, *colour >= 0 ? *colour : 3)));
    CLCOMMANDM(colour, "sbgf", (char *who, int *m, float *f, float *x), intret(game::getcolour(d, *m, *f >= 0 && *f <= 10 ? *f : 1.f, *x)));
    CLCOMMANDM(vitem, "sbi", (char *who, int *n, int *v), getvitem(d, *n, *v));

    CLCOMMAND(weapselect, intret(d->weapselect));
    CLCOMMANDM(loadweap, "si", (char *who, int *n), intret(d->loadweap.inrange(*n) ? d->loadweap[*n] : -1));
    CLCOMMANDM(weapget, "siii", (char *who, int *n, int *a, int *b), intret(isweap(*n) ? d->getammo(*n, *a!=0 ? lastmillis : 0, *b!=0) : -1));
    CLCOMMANDM(weapammo, "sii", (char *who, int *n, int *m), intret(isweap(*n) ? d->weapammo[*n][clamp(*m, 0, W_A_MAX-1)] : -1));
    CLCOMMANDM(weapclip, "si", (char *who, int *n), intret(isweap(*n) ? d->weapammo[*n][W_A_CLIP] : -1));
    CLCOMMANDM(weapstore, "si", (char *who, int *n), intret(isweap(*n) ? d->weapammo[*n][W_A_STORE] : -1));
    CLCOMMANDM(weapstate, "si", (char *who, int *n), intret(isweap(*n) ? d->weapstate[*n] : W_S_IDLE));
    CLCOMMANDM(weaptime, "si", (char *who, int *n), intret(isweap(*n) ? d->weaptime[*n] : 0));
    CLCOMMANDM(weapwait, "si", (char *who, int *n), intret(isweap(*n) ? d->weapwait[*n] : 0));
    CLCOMMANDM(weapload, "sii", (char *who, int *n, int *m), intret(isweap(*n) ? d->weapload[*n][clamp(*m, 0, W_A_MAX-1)] : 0));
    CLCOMMANDM(weaphold, "si", (char *who, int *n), intret(isweap(*n) && d->holdweap(*n, m_weapon(d->actortype, game::gamemode, game::mutators), lastmillis) ? 1 : 0));
    CLCOMMAND(weapholdnum, intret(d->holdweapcount(m_weapon(d->actortype, game::gamemode, game::mutators), lastmillis)));
    CLCOMMANDM(action, "sb", (char *who, int *n), intret(d->action[clamp(*n, 0, int(AC_MAX-1))] ? 1 : 0));
    CLCOMMANDM(actiontime, "sb", (char *who, int *n), intret(d->actiontime[clamp(*n, 0, int(AC_MAX-1))]));

    CLCOMMAND(move, intret(d->move));
    CLCOMMAND(strafe, intret(d->strafe));
    CLCOMMAND(physstate, intret(d->physstate));
    CLCOMMAND(turnside, intret(d->turnside));
    CLCOMMAND(physstate, intret(d->physstate));
    CLCOMMAND(lastdeath, intret(d->lastdeath));
    CLCOMMAND(lastspawn, intret(d->lastspawn));
    CLCOMMAND(lastbuff, intret(d->lastbuff));
    CLCOMMAND(lastshoot, intret(d->lastshoot));
    CLCOMMAND(lastattacker, intret(d->lastattacker));
    CLCOMMAND(lasthacker, intret(d->lasthacker));
    CLCOMMAND(airmillis, intret(d->airmillis));
    CLCOMMAND(airtime, intret(d->airtime(lastmillis)));
    CLCOMMAND(floormillis, intret(d->floormillis));
    CLCOMMAND(floortime, intret(d->floortime(lastmillis)));
    CLCOMMAND(inmaterial, intret(d->inmaterial));
    CLCOMMAND(inliquid, intret(isliquid(d->inmaterial&MATF_VOLUME) ? 1 : 0));
    CLCOMMAND(hasliquid, intret(physics::liquidcheck(d) ? 1 : 0));
    CLCOMMAND(onladder, intret(isladder(d->inmaterial) ? 1 : 0));
    CLCOMMAND(hasladder, intret(physics::laddercheck(d) ? 1 : 0));
    CLCOMMAND(headless, intret(d->headless ? 1 : 0));
    CLCOMMAND(obliterated, intret(d->obliterated ? 1 : 0));
    CLCOMMAND(actortype, intret(d->actortype));
    CLCOMMAND(pcolour, intret(d->colours[0]));
    CLCOMMAND(colour2, intret(d->colours[1]));
    CLCOMMAND(model, intret(d->model%PLAYERTYPES));
    CLCOMMAND(vanity, result(d->vanity));
    CLCOMMAND(mixer, result(d->mixer));
    CLCOMMAND(obit, result(d->obit));
    CLCOMMAND(handle, result(d->handle));
    CLCOMMAND(steamid, result(d->steamid));
    CLCOMMAND(host, result(d->hostip));
    CLCOMMAND(ip, result(d->hostip));
    CLCOMMAND(ping, intret(d->ping));
    CLCOMMAND(pj, intret(d->plag));
    CLCOMMAND(team, intret(d->team));
    CLCOMMAND(state, intret(d->state));
    CLCOMMAND(health, intret(d->health));
    CLCOMMAND(points, intret(d->points));
    CLCOMMAND(cptime, intret(d->cptime));
    CLCOMMAND(cplast, intret(d->cplast));
    CLCOMMAND(cpmillis, intret(d->cpmillis ? lastmillis-d->cpmillis : 0));
    CLCOMMAND(frags, intret(d->frags));
    CLCOMMAND(deaths, intret(d->deaths));

    CLCOMMAND(conopen, intret(d->conopen ? 1 : 0));
    CLCOMMAND(queuepos, intret(d->queuepos));

    CLCOMMAND(totalpoints, intret(d->totalpoints));
    CLCOMMAND(totalfrags, intret(d->totalfrags));
    CLCOMMAND(totaldeaths, intret(d->totaldeaths));
    CLCOMMAND(balancescore, floatret(d->balancescore()));
    CLCOMMAND(timeplayed, intret(d->updatetimeplayed()));
    CLCOMMAND(timeplayed, intret(d->updatetimeplayed()));

    CLCOMMAND(speed, floatret(d->speed));
    CLCOMMAND(impulsespeed, floatret(d->impulsespeed));
    CLCOMMAND(weight, floatret(d->weight));
    CLCOMMAND(buoyancy, floatret(d->buoyancy));

    CLCOMMAND(movescale, floatret(d->movescale));
    CLCOMMAND(gravityscale, floatret(d->gravityscale));
    CLCOMMAND(coastscale, floatret(d->coastscale));

    CLCOMMAND(scoretime, floatret(d->scoretime()));
    CLCOMMANDM(kdratio, "si", (char *who, int *n), intret(d->kdratio(*n!=0)));

    CLCOMMAND(allowimpulse, intret(A(d->actortype, impulse)&IM_T_CHECK ? 1 : 0));
    CLCOMMAND(impulsecostmeter, intret(d->impulse[IM_METER]));
    CLCOMMAND(impulsetype, intret(d->impulse[IM_TYPE]));
    CLCOMMANDM(impulsetime, "sb", (char *who, int *n), intret(d->impulsetime[*n >= 0 && *n < IM_T_MAX ? *n : (d->impulse[IM_TYPE] >= 0 && d->impulse[IM_TYPE] < IM_T_MAX ? d->impulse[IM_TYPE] : IM_T_JUMP)]));
    CLCOMMAND(impulseregen, intret(d->impulse[IM_REGEN]));
    CLCOMMAND(impulsecount, intret(d->impulse[IM_COUNT]));
    CLCOMMAND(impulsejump, intret(d->impulsetime[IM_T_JUMP]));
    CLCOMMAND(impulsewait, intret(d->impulsetime[IM_T_PUSHER]));
    CLCOMMANDM(impulse, "si", (char *who, int *n), intret(*n >= 0 && *n < IM_MAX ? d->impulse[*n] : 0));

    CLCOMMANDM(impulsemask, "sb", (char *who, int *n), intret(d->impulsemask(*n)));
    CLCOMMANDM(impulselen, "sb", (char *who, int *n), intret(d->impulselen(*n)));
    CLCOMMANDM(impulsedelay, "sb", (char *who, int *n), intret(d->impulsedelay(*n)));
    CLCOMMANDM(impulsetimer, "sbiii", (char *who, int *n, int *c, int *a, int *r), intret(d->impulsetimer(*n, *c != 0, *a != 0, *r)));
    CLCOMMANDM(impulseready, "si", (char *who, int *n), intret(d->impulseready(*n)));
    CLCOMMANDM(canimpulse, "si", (char *who, int *n), intret(d->canimpulse(*n) ? 1 : 0));
    CLCOMMAND(regenimpulse, intret(d->regenimpulse() ? 1 : 0));
    CLCOMMANDM(slide, "s", (char *who), intret(d->hasslide() ? 1 : 0));
    CLCOMMANDM(crouching, "s", (char *who), intret(d->crouching() ? 1 : 0));

    CLCOMMAND(buffing, intret(d->lastbuff));
    CLCOMMAND(hasprize, intret(d->hasprize));
    CLCOMMAND(isprize, intret(d->isprize(game::focus)));
    CLCOMMAND(ishighlight, intret(d->ishighlight(game::focus)));
    CLCOMMAND(collects, intret(d->collects.length()));

    CLCOMMAND(burnfunc, intret(d->burntime ? d->burnfunc(lastmillis, d->burntime) : 0));
    CLCOMMAND(bleedfunc, intret(d->bleedtime ? d->bleedfunc(lastmillis, d->bleedtime) : 0));
    CLCOMMAND(shockfunc, intret(d->shocktime ? d->shockfunc(lastmillis, d->shocktime) : 0));
    CLCOMMAND(corrodefunc, intret(d->corrodetime ? d->corrodefunc(lastmillis, d->corrodetime) : 0));

    CLCOMMAND(burntime, intret(d->burntime));
    CLCOMMAND(bleedtime, intret(d->bleedtime));
    CLCOMMAND(shocktime, intret(d->shocktime));
    CLCOMMAND(corrodetime, intret(d->corrodetime));

    CLCOMMAND(regen, intret(regentime ? d->lastregen : 0));
    CLCOMMAND(regenamt, intret(d->lastregenamt));
    CLCOMMAND(impulsecollectcount, intret(d->impulse[IM_COLLECT_COUNT]));
    CLCOMMAND(impulselastcount, intret(d->impulse[IM_LASTCOL_COUNT] ? (lastmillis - d->impulse[IM_LASTCOL_COUNT]) % 1000 : 0));
    CLCOMMAND(impulsecollectmeter, intret(d->impulse[IM_COLLECT_METER]));
    CLCOMMAND(impulselastmeter, intret(d->impulse[IM_LASTCOL_METER] ? (lastmillis - d->impulse[IM_LASTCOL_METER]) % 1000 : 0));

    CLCOMMAND(spawnweap, intret(m_weapon(d->actortype, game::gamemode, game::mutators)));
    CLCOMMAND(spawndelay, intret(m_delay(d->actortype, game::gamemode, game::mutators, d->team)));
    CLCOMMAND(spawnprotect, intret(m_protect(game::gamemode, game::mutators)));
    CLCOMMAND(spawnhealth, intret(d->gethealth(game::gamemode, game::mutators)));
    CLCOMMAND(maxhealth, intret(d->gethealth(game::gamemode, game::mutators, true)));

    CLCOMMAND(respawnwait, intret(d->isnotalive() ? d->respawnwait(lastmillis, m_delay(d->actortype, game::gamemode, game::mutators, d->team)): -1));
    CLCOMMANDM(canshoot, "sbi", (char *who, int *weap, int *alt), intret(d->canshoot(*weap >= 0 ? *weap : d->weapselect, *alt > 0 ? HIT_ALT : 0, m_weapon(d->actortype, game::gamemode, game::mutators), lastmillis, (1<<W_S_RELOAD)) ? 1 : 0));
    CLCOMMANDM(canreload, "sb", (char *who, int *weap), intret(d->canreload(*weap >= 0 ? *weap : d->weapselect, m_weapon(d->actortype, game::gamemode, game::mutators), lastmillis, (1<<W_S_RELOAD)) ? 1 : 0));

    CLCOMMANDM(canuse, "si", (char *who, int *n),
    {
        if(entities::ents.inrange(*n))
        {
            extentity &e = *entities::ents[*n];
            if(enttype[e.type].usetype == EU_ITEM && e.type == WEAPON && entities::isallowed(e))
            {
                int attr = m_attr(e.type, e.attrs[0]);
                if(isweap(attr) && d->canuse(game::gamemode, game::mutators, e.type, attr, e.attrs, m_weapon(d->actortype, game::gamemode, game::mutators), lastmillis, (1<<W_S_SWITCH)|(1<<W_S_RELOAD)))
                {
                    intret(1);
                    return;
                }
            }
        }

        intret(0);
    });


    CLCOMMANDM(pulsecolour, "sib", (char *who, int *n, int *c), intret(pulsehexcol(d, *n, *c >= -1 ? *c : PULSE_CYCLE)));
    CLCOMMANDM(velocity, "si", (char *who, int *n), floatret(vec(d->vel).add(d->falling).magnitude()*(*n!=0 ? (*n > 0 ? 3.6f/8.f : 0.125f) : 1.f)));

    CLCOMMANDM(residualfx, "sib", (char *who, int *n, int *c), intret(getresidualfx(d, *n, *c)));

    #define CLDOMCMD(dtype) \
        CLCOMMANDM(dtype, "sb", (char *who, int *n), \
        { \
            if(*n < 0) intret(d->dtype.length()); \
            else if(d->dtype.inrange(*n)) intret(d->dtype[*n]->clientnum); \
        });
    CLDOMCMD(dominator);

    #define CLISDOMCMD(dtype) \
        CLCOMMANDMK(is##dtype, "ss", (char *who, char *n), \
        { \
            gameent *e = game::getclient(client::parseplayer(n)); \
            if(!e) \
            { \
                intret(0); \
                return; \
            } \
            loopv(d->dtype) if(d->dtype[i]->clientnum == e->clientnum) \
            { \
                intret(1); \
                return; \
            } \
            intret(0); \
            return; \
        }, intret(0); return);
    CLISDOMCMD(dominator);

    CLCOMMAND(privilege, intret(d->privilege&PRIV_TYPE));
    CLCOMMAND(privlocal, intret(d->privilege&PRIV_LOCAL ? 1 : 0));
    CLCOMMAND(privtex, result(hud::privtex(d->privilege, d->actortype)));

    #define CLPRIVCMD(pname,pval) CLCOMMAND(priv##pname, intret(haspriv(d, pval) ? 1 : 0));
    CLPRIVCMD(none, PRIV_NONE);
    CLPRIVCMD(player, PRIV_PLAYER);
    CLPRIVCMD(supporter, PRIV_SUPPORTER);
    CLPRIVCMD(moderator, PRIV_MODERATOR);
    CLPRIVCMD(administrator, PRIV_ADMINISTRATOR);
    CLPRIVCMD(developer, PRIV_DEVELOPER);
    CLPRIVCMD(founder, PRIV_CREATOR);
    CLCOMMANDM(priv, "si", (char *who, int *priv), intret(haspriv(d, clamp(*priv, 0, PRIV_MAX-1)) ? 1 : 0));

    CLCOMMANDM(running, "s", (char *who), intret(d->running() ? 1 : 0));
    CLCOMMANDM(sprinting, "sb", (char *who, int *n), intret(d->sprinting(*n != 0) ? 1 : 0));
    CLCOMMAND(sprinttime, intret(d->sprinttime));
    CLCOMMAND(rotvelx, floatret(d->rotvel.x));
    CLCOMMAND(rotvely, floatret(d->rotvel.y));
}