#include "engine.h"

#define BASERESOLUTION 3840.f // base resolution for scaling

static void gscaledims(int &w, int &h, float scale = 1.0f)
{
    w = max(int(ceilf(w * scale)), 1);
    h = max(int(ceilf(h * scale)), 1);

    if(gscale != 100)
    {
        w = max((w * gscale + 99) / 100, 1);
        h = max((h * gscale + 99) / 100, 1);
    }
}

bool RenderSurface::cleanup()
{
    buffers.deletecontents();
    return true;
}

void RenderSurface::checkformat(int &w, int &h, GLenum &f, GLenum &t, int &n)
{
    w = max(int(w > 0 ? w : vieww), 1);
    h = max(int(h > 0 ? h : viewh), 1);
}

int RenderSurface::setup(int w, int h, GLenum f, GLenum t, int count)
{
    GLuint curfbo = renderfbo;
    bool restore = false;

    loopi(count)
    {
        if(buffers.inrange(i))
        {
            if(buffers[i]->check(w, h, f, t))
                restore = true;
            continue;
        }

        buffers.add(new RenderBuffer(w, h, f, t));
        restore = true;
    }

    if(restore) glBindFramebuffer_(GL_FRAMEBUFFER, curfbo);

    return buffers.length();
}

bool RenderSurface::bindtex(int index, int tmu)
{
    if(!buffers.inrange(index)) return false;

    if(tmu >= 0) glActiveTexture_(GL_TEXTURE0 + tmu);
    glBindTexture(buffers[index]->target, buffers[index]->tex);

    return true;
}

void RenderSurface::savefbo()
{
    origfbo = renderfbo;
    origvieww = vieww;
    origviewh = viewh;
}

bool RenderSurface::bindfbo(int index, int w, int h)
{
    if(!buffers.inrange(index)) return false;
    if(renderfbo == buffers[index]->fbo) return true;

    renderfbo = buffers[index]->fbo;
    vieww = w > 0 && w < buffers[index]->width ? w : buffers[index]->width;
    viewh = h > 0 && h < buffers[index]->height ? h : buffers[index]->height;

    GLERROR;
    glBindFramebuffer_(GL_FRAMEBUFFER, renderfbo);
    glViewport(0, 0, vieww, viewh);

    return true;
}

int RenderSurface::create(int w, int h, GLenum f, GLenum t, int count)
{
    checkformat(w, h, f, t, count);
    return setup(w, h, f, t, count);
}

bool RenderSurface::destroy() { return cleanup(); }

bool RenderSurface::render(int w, int h, GLenum f, GLenum t, int count) { return false; }

bool RenderSurface::swap(int index) { return bindfbo(index); }

bool RenderSurface::draw(int x, int y, int w, int h) { return false; }

void RenderSurface::debug(int w, int h, int index, bool large)
{
    if(hasnoview() || buffers.empty()) return;

    index = max(index > 0 ? clamp(index, 0, buffers.length()) : buffers.length(), 1);

    int sw = w / (large ? index : index * 2), sx = 0, sh = (sw * h) / w;

    loopi(index)
    {
        bool hastex = buffers.inrange(i);
        GLuint tex = hastex ? buffers[i]->tex : notexture->id;
        GLenum targ = hastex ? buffers[i]->target : GL_TEXTURE_2D;

        switch(targ)
        {
            case GL_TEXTURE_RECTANGLE:
            {
                SETSHADER(hudrectrgb);
                break;
            }
            case GL_TEXTURE_2D:
            {
                SETSHADER(hudrgb);
                break;
            }
            default: continue;
        }

        gle::colorf(1, 1, 1);
        glBindTexture(targ, tex);
        debugquad(sx, 0, sw, sh, 0, 0, buffers[i]->width, buffers[i]->height);
        sx += sw;
    }
}

bool RenderSurface::save(const char *name, int w, int h, int index)
{
    savefbo();
    if(!bindfbo(index)) return false;

    GLuint tex;
    glGenTextures(1, &tex);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    ImageData image(vieww, viewh, 3);
    memset(image.data, 0, 3*vieww*viewh);

    glReadPixels(0, 0, vieww, viewh, GL_RGB, GL_UNSIGNED_BYTE, image.data);

    if(w != vieww || h != viewh) scaleimage(image, w, h);
    saveimage(name, image, imageformat, compresslevel, true);
    glDeleteTextures(1, &tex);

    restorefbo();

    return true;
}

void RenderSurface::restorefbo()
{
    renderfbo = origfbo;
    vieww = origvieww;
    viewh = origviewh;

    GLERROR;
    glBindFramebuffer_(GL_FRAMEBUFFER, renderfbo);
    glViewport(0, 0, vieww, viewh);
}

bool RenderSurface::copy(int index, GLuint fbo, int w, int h, bool linear, bool restore)
{
    if(!buffers.inrange(index)) return false;
    GLuint curfbo = renderfbo;

    GLERROR;
    glBindFramebuffer_(GL_READ_FRAMEBUFFER, fbo);
    glBindFramebuffer_(GL_DRAW_FRAMEBUFFER, buffers[index]->fbo);
    glBlitFramebuffer_(0, 0, w, h, 0, 0, buffers[index]->width, buffers[index]->height, GL_COLOR_BUFFER_BIT, linear ? GL_LINEAR : GL_NEAREST);

    GLERROR;
    if(restore) glBindFramebuffer_(GL_FRAMEBUFFER, curfbo);

    return true;
}


VAR(0, debughalo, 0, 0, 2);
VAR(IDF_PERSIST, halos, 0, 1, 1);
FVAR(IDF_PERSIST, halowireframe, 0, 0, FVAR_MAX);
VAR(IDF_PERSIST, halodist, 32, 2048, VAR_MAX);
FVARF(IDF_PERSIST, haloscale, FVAR_NONZERO, 1, 1, halosurf.destroy());
FVAR(IDF_PERSIST, haloblend, 0, 1, 1);
FVAR(IDF_PERSIST, halobright, 0, 2, 16);
FVAR(IDF_PERSIST, halobrightinfill, 0, 0.5f, 16);
FVAR(IDF_PERSIST, halotolerance, FVAR_MIN, -16, FVAR_MAX);
FVAR(IDF_PERSIST, haloaddz, FVAR_MIN, 0, FVAR_MAX);
VAR(IDF_PERSIST, haloiter, 0, 0, 4);
VAR(IDF_PERSIST, haloradius, 0, 4, MAXBLURRADIUS);

HaloSurface halosurf;

void HaloSurface::checkformat(int &w, int &h, GLenum &f, GLenum &t, int &n)
{
    w = renderw;
    h = renderh;
    gscaledims(w, h, haloscale);
    n = MAX;
}

int HaloSurface::create(int w, int h, GLenum f, GLenum t, int count)
{
    useshaderbyname("hudhalobuild");
    useshaderbyname("hudhalodraw");

    checkformat(w, h, f, t, count);

    return setup(w, h, f, t, count);
}

bool HaloSurface::check()
{
    if(!halos || buffers.empty()) return false;
    return true;
}

bool HaloSurface::render(int w, int h, GLenum f, GLenum t, int count)
{
    if(!halos || hasnoview() || !create(w, h, f, t, count)) return false;

    int olddrawtex = drawtex;
    drawtex = DRAWTEX_HALO;

    projmatrix.perspective(fovy, aspect, nearplane, farplane);
    setcamprojmatrix();

    savefbo();

    loopirev(MAX)
    {   // reverse order to avoid unnecessary swaps
        swap(i);

        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    glEnable(GL_CULL_FACE);

    if(halowireframe > 0)
    {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glLineWidth(halowireframe);
    }

    loopi(2)
    { // two passes for halos so we can determine actor tags first
        resetmodelbatches();
        game::render(i + 1);

        renderhalomodelbatches(false);
        swap(ONTOP);
        renderhalomodelbatches(true);
        if(!i)
        {
            renderavatar();
            swap(DEPTH);
        }
    }

    if(halowireframe > 0)
    {
        glLineWidth(1);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

    glDisable(GL_CULL_FACE);

    drawtex = olddrawtex;
    restorefbo();

    return true;
}

bool HaloSurface::build(int x, int y, int w, int h)
{
    if(!halos || hasnoview() || buffers.empty()) return false;

    savefbo();
    if(!bindfbo(COMBINE)) return false;

    float maxdist = hud::radarlimit(halodist);
    vec2 halodepth = renderdepthscale(vieww, viewh);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ZERO);

    hudmatrix.ortho(0, vieww, viewh, 0, -1, 1);
    flushhudmatrix();
    resethudshader();

    SETSHADER(hudhalobuild);

    glActiveTexture_(GL_TEXTURE0 + TEX_EARLY_DEPTH);
    if(msaasamples) glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, msearlydepthtex);
    else glBindTexture(GL_TEXTURE_RECTANGLE, earlydepthtex);

    loopirev(ONTOP + 1) bindtex(i, i);
    LOCALPARAMF(halodepth, halodepth.x, halodepth.y, 1.0f / halodepth.x, 1.0f / halodepth.y);
    LOCALPARAMF(halomaxdist, maxdist);
    hudquad(0, 0, vieww, viewh, 0, viewh, vieww, -viewh);

    int radius = int(ceilf(haloradius * (max(buffers[DEPTH]->width, buffers[DEPTH]->height) / BASERESOLUTION)));
    if(radius)
    {
        float blurweights[MAXBLURRADIUS+1], bluroffsets[MAXBLURRADIUS+1];
        setupblurkernel(radius, blurweights, bluroffsets);

        loopi(2 + 2*haloiter)
        {
            if(!bindfbo(DEPTH + ((i + 1) % 2))) continue;
            setblurshader(i % 2, 1, radius, blurweights, bluroffsets, GL_TEXTURE_RECTANGLE, true);
            bindtex(COMBINE, 1);
            bindtex(!i ? COMBINE : DEPTH + (i % 2), 0);
            screenquad(vieww, viewh);
        }
    }

    glDisable(GL_BLEND);
    restorefbo();

    return true;
}

bool HaloSurface::draw(int x, int y, int w, int h)
{
    if(!halos || hasnoview() || buffers.empty()) return false;

    float maxdist = hud::radarlimit(halodist);
    vec2 halodepth = renderdepthscale(vieww, viewh);

    if(w <= 0) w = vieww;
    if(h <= 0) h = viewh;

    SETSHADER(hudhalodraw);
    gle::colorf(1, 1, 1, haloblend);

    glActiveTexture_(GL_TEXTURE0 + TEX_EARLY_DEPTH);
    if(msaasamples) glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, msearlydepthtex);
    else glBindTexture(GL_TEXTURE_RECTANGLE, earlydepthtex);
    bindtex(COMBINE, 1);
    bindtex(DEPTH, 0);

    LOCALPARAMF(halodepth, halodepth.x, halodepth.y, 1.0f / halodepth.x, 1.0f / halodepth.y);
    LOCALPARAMF(haloparams, maxdist, halobright, halobrightinfill);
    hudquad(x, y, w, h, 0, buffers[DEPTH]->height, buffers[DEPTH]->width, -buffers[DEPTH]->height);

    return true;
}

#define MPVVARS(name, type) \
    VARF(IDF_MAP, haze##name, 0, 0, 1, hazesurf.create()); \
    CVAR(IDF_MAP, hazecolour##name, 0); \
    FVAR(IDF_MAP, hazecolourmix##name, 0, 0.5f, 1); \
    FVAR(IDF_MAP, hazeblend##name, 0, 1, 1); \
    SVARF(IDF_MAP, hazetex##name, "textures/watern", hazesurf.create()); \
    FVAR(IDF_MAP, hazemindist##name, 0, 256, FVAR_MAX); \
    FVAR(IDF_MAP, hazemaxdist##name, 0, 1024, FVAR_MAX); \
    FVAR(IDF_MAP, hazemargin##name, FVAR_NONZERO, 32, FVAR_MAX); \
    FVAR(IDF_MAP, hazescalex##name, FVAR_NONZERO, 1, FVAR_MAX); \
    FVAR(IDF_MAP, hazescaley##name, FVAR_NONZERO, 2, FVAR_MAX); \
    FVAR(IDF_MAP, hazerefract##name, FVAR_NONZERO, 2, 10); \
    FVAR(IDF_MAP, hazerefract2##name, FVAR_NONZERO, 4, 10); \
    FVAR(IDF_MAP, hazerefract3##name, FVAR_NONZERO, 8, 10); \
    FVAR(IDF_MAP, hazescrollx##name, FVAR_MIN, 0, FVAR_MAX); \
    FVAR(IDF_MAP, hazescrolly##name, FVAR_MIN, -0.5f, FVAR_MAX);

MPVVARS(, MPV_DEFAULT);
MPVVARS(alt, MPV_ALTERNATE);

#define GETMPV(name, type) \
    type get##name() \
    { \
        if(checkmapvariant(MPV_ALTERNATE)) return name##alt; \
        return name; \
    }

GETMPV(haze, int);
GETMPV(hazetex, const char *);
GETMPV(hazecolour, const bvec &);
GETMPV(hazecolourmix, float);
GETMPV(hazeblend, float);
GETMPV(hazemindist, float);
GETMPV(hazemaxdist, float);
GETMPV(hazemargin, float);
GETMPV(hazescalex, float);
GETMPV(hazescaley, float);
GETMPV(hazerefract, float);
GETMPV(hazerefract2, float);
GETMPV(hazerefract3, float);
GETMPV(hazescrollx, float);
GETMPV(hazescrolly, float);

VAR(0, debughaze, 0, 0, 2);

HazeSurface hazesurf;

void HazeSurface::checkformat(int &w, int &h, GLenum &f, GLenum &t, int &n)
{
    w = max(int((w > 0 ? w : vieww)), 1);
    h = max(int((h > 0 ? h : viewh)), 1);
    f = hdrformat;
}

bool HazeSurface::check()
{
    return gethaze() != 0 || hazeparticles;
}

int HazeSurface::create(int w, int h, GLenum f, GLenum t, int count)
{
    tex = NULL;
    if(!check()) return -1;

    const char *hazename = gethazetex();
    if(hazename[0]) tex = textureload(hazename, 0, true, false);
    if(tex == notexture) tex = NULL;

    if(tex)
    {
        useshaderbyname("hazetex");
        useshaderbyname("hazetexref");
    }
    else
    {
        useshaderbyname("haze");
        useshaderbyname("hazeref");
    }

    checkformat(w, h, f, t, count);

    return setup(w, h, f, t, count);
}

bool HazeSurface::render(int w, int h, GLenum f, GLenum t, int count)
{
    if(!create(w, h, f, t, count)) return false;

    if(tex || hazeparticles)
    {
        if(msaalight)
        {
            glBindFramebuffer_(GL_READ_FRAMEBUFFER, mshdrfbo);
            glBindFramebuffer_(GL_DRAW_FRAMEBUFFER, buffers[0]->fbo);
            glBlitFramebuffer_(0, 0, buffers[0]->width, buffers[0]->height, 0, 0, buffers[0]->width, buffers[0]->height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glBindFramebuffer_(GL_FRAMEBUFFER, mshdrfbo);
        }
        else
        {
            bindtex(0, 0);
            glCopyTexSubImage2D(GL_TEXTURE_RECTANGLE, 0, 0, 0, 0, 0, buffers[0]->width, buffers[0]->height);
        }
    }

    bool hazemix = false;
    if(gethaze() != 0)
    {
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        if(tex)
        {
            glActiveTexture_(GL_TEXTURE0 + TEX_CURRENT_LIGHT);
            glBindTexture(GL_TEXTURE_RECTANGLE, buffers[0]->tex);
        }

        glActiveTexture_(GL_TEXTURE0 + TEX_EARLY_DEPTH);
        if(msaalight) glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, msearlydepthtex);
        else glBindTexture(GL_TEXTURE_RECTANGLE, earlydepthtex);

        glActiveTexture_(GL_TEXTURE0);

        const bvec &color = worldcols[WORLDCOL_F_HAZE];
        float colormix = gethazecolourmix();
        if(tex && color.iszero()) colormix = 0;
        else hazemix = true;
        GLOBALPARAMF(hazecolor, color.x*ldrscaleb, color.y*ldrscaleb, color.z*ldrscaleb, colormix);
        float refract = gethazerefract(), refract2 = gethazerefract2(), refract3 = gethazerefract3();
        GLOBALPARAMF(hazerefract, refract, refract2, refract3);
        float margin = gethazemargin(), mindist = gethazemindist(), maxdist = max(max(mindist, gethazemaxdist())-mindist, margin), blend = gethazeblend();
        GLOBALPARAMF(hazeparams, mindist, 1.0f/maxdist, 1.0f/margin, blend);

        if(tex)
        {
            float xscale = gethazescalex(), yscale = gethazescaley(), scroll = lastmillis/1000.0f, xscroll = gethazescrollx()*scroll, yscroll = gethazescrolly()*scroll;
            GLOBALPARAMF(hazetexgen, xscale, yscale, xscroll, yscroll);
            settexture(tex);
            SETSHADER(hazetex);
        }
        else { SETSHADER(haze); }

        screenquad();

        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
    }

    if(hazeparticles) renderhazeparticles(buffers[0]->tex, hazemix);

    return true;
}

VAR(0, debugvisor, 0, 0, 2);

VAR(IDF_PERSIST, visorglassallow, 0, 2, 2);
bool hasglass() { return visorglassallow && (visorglassallow >= 2 || hud::hasinput(true)); }

VAR(IDF_PERSIST, visorglassiter, 0, 0, 4);
VAR(IDF_PERSIST, visorglassradius, 0, 8, MAXBLURRADIUS);
VAR(IDF_PERSIST, visorglassiterload, 0, 0, 4);
VAR(IDF_PERSIST, visorglassradiusload, 0, 8, MAXBLURRADIUS);
FVAR(IDF_PERSIST, visorglassscale, FVAR_NONZERO, 0.25f, 1.f);
FVAR(IDF_PERSIST, visorglassmix, FVAR_NONZERO, 4, FVAR_MAX);
FVAR(IDF_PERSIST, visorglassmin, 0, 0, 1);
FVAR(IDF_PERSIST, visorglassmax, 0, 1, 1);

VAR(IDF_PERSIST, visorrender, 0, 1, 2); // 0 = off, 1 = on except editing, 2 = always on
FVAR(IDF_PERSIST, visorrenderchromamin, 0, 1.5f, FVAR_MAX);
FVAR(IDF_PERSIST, visorrenderchromamax, 0, 8.0f, FVAR_MAX);
FVAR(IDF_PERSIST, visorrenderchromascale, 0, 0.25f, FVAR_MAX);
FVAR(IDF_PERSIST, visorblitchromamin, 0, 0.0f, FVAR_MAX);
FVAR(IDF_PERSIST, visorblitchromamax, 0, 32.0f, FVAR_MAX);
FVAR(IDF_PERSIST, visorblitchromascale, 0, 1.0f, FVAR_MAX);

VAR(IDF_PERSIST, visorhud, 0, 1, 1);
FVAR(IDF_PERSIST, visordistort, -2, 2, 2);
FVAR(IDF_PERSIST, visornormal, -2, 1.175f, 2);
FVAR(IDF_PERSIST, visorscalex, FVAR_NONZERO, 0.9075f, 2);
FVAR(IDF_PERSIST, visorscaley, FVAR_NONZERO, 0.9075f, 2);

ICOMMANDV(0, visorenabled, visorsurf.check() ? 1 : 0);

VAR(IDF_PERSIST, showloadingaspect, 0, 1, 1);
VAR(IDF_PERSIST, showloadingmapbg, 0, 1, 1);
VAR(IDF_PERSIST, showloadinglogos, 0, 1, 1);

CVAR(IDF_PERSIST, backgroundcolour, 0x200000);
TVAR(IDF_PERSIST|IDF_PRELOAD, backgroundwatertex, "<grey><noswizzle>textures/water", 0x300);
TVAR(IDF_PERSIST|IDF_PRELOAD, backgroundcausttex, "<comp>caustic", 0x300);
TVAR(IDF_PERSIST|IDF_PRELOAD, backgroundtex, "<nocompress>textures/menubg", 3);
TVAR(IDF_PERSIST|IDF_PRELOAD, backgroundmasktex, "<nocompress>textures/menubg_mask", 3);

VisorSurface visorsurf;
VARR(rendervisor, -1);

bool VisorSurface::drawnoview()
{
    float level = game::darkness(DARK_UI);

    gle::colorf(level, level, level, 1);

    Texture *t = NULL;
    if(engineready && showloadingmapbg && *mapname && strcmp(mapname, "maps/untitled"))
        t = textureload(mapname, 3, true, false);

    config.reset();

    if(!engineready || !t || t == notexture)
    {
        pushhudmatrix();
        hudmatrix.ortho(0, 1, 1, 0, -1, 1);
        flushhudmatrix();

        t = textureload(backgroundtex, 3, true, false);

        if(t)
        {
            if(engineready && hudbackgroundshader)
            {
                hudbackgroundshader->set();
                LOCALPARAMF(time, lastmillis / 1000.0f);
                LOCALPARAMF(aspect, viewh / float(vieww));

                glActiveTexture_(GL_TEXTURE0);
                settexture(t);
                glActiveTexture_(GL_TEXTURE1);
                settexture(backgroundwatertex, 0x300);
                glActiveTexture_(GL_TEXTURE2);
                settexture(backgroundcausttex, 0x300);
                glActiveTexture_(GL_TEXTURE3);
                settexture(backgroundmasktex, 3);

                glActiveTexture_(GL_TEXTURE0);
            }
            else
            {
                glActiveTexture_(GL_TEXTURE0);
                settexture(t);
            }

            // Calculate cropping of the background
            float viewratio = viewh / float(vieww), bgratio = t->h / float(t->w);

            if(viewratio < bgratio)
            {
                float scalex = vieww / float(t->w);
                float scaledh = t->h * scalex;
                float ratioy = viewh / scaledh;
                config.offsety = (1.0f - ratioy) * 0.5f;
            }
            else
            {
                float scaley = viewh / float(t->h);
                float scaledw = t->w * scaley;
                float ratiox = vieww / scaledw;
                config.offsetx = (1.0f - ratiox) * 0.5f;
            }
        }
        else if(hudnotextureshader)
        {
            hudnotextureshader->set();
            gle::color(backgroundcolour.tocolor().mul(level), 1.f);
        }
        else
        {
            pophudmatrix();
            resethudshader();

            return false;
        }

        hudquad(0, 0, 1, 1, config.offsetx, config.offsety, 1.0f - config.offsetx - config.offsetx, 1.0f - config.offsety - config.offsety);

        pophudmatrix();
        resethudshader();

        return false;
    }

    settexture(t);

    if(showloadingaspect)
    {
        if(vieww > viewh) config.offsety = ((vieww - viewh) / float(vieww)) * 0.5f;
        else if(viewh > vieww) config.offsetx = ((viewh - vieww) / float(viewh)) * 0.5f;
    }

    hudquad(0, 0, vieww, viewh, config.offsetx, config.offsety, 1.0f - config.offsetx, 1.0f - config.offsety);

    return true;
}

void VisorSurface::drawprogress()
{
    if(!progressing || engineready) return;

    if(showloadinglogos)
    {
        gle::colorf(1, 1, 1, 1);

        Texture *t = textureload(logotex, 3, true, false);
        settexture(t);
        hudquad(vieww - vieww/4 - vieww/3, viewh/2 - vieww/6, vieww/2, vieww/4);
    }

    float oldtextscale = curtextscale;
    pushfont(textfontlogo);
    curtextscale = 0.75f;
    draw_textf("%s", FONTH/2, viewh - FONTH * 5 / 4, 0, 0, 255, 255, 255, 255, TEXT_LEFT_JUSTIFY, -1, -1, 1, game::getprogresstitle());
    if(progressamt > 0) draw_textf("[ %.1f%% ]", vieww - FONTH/2, viewh - FONTH * 5/4, 0, 0, 255, 255, 255, 255, TEXT_RIGHT_JUSTIFY, -1, -1, 1, progressamt*100);
    curtextscale = oldtextscale;
    popfont();
    resethudshader();
}

void VisorSurface::checkformat(int &w, int &h, GLenum &f, GLenum &t, int &n)
{
    w = max(int((w > 0 ? w : vieww)), 1);
    h = max(int((h > 0 ? h : viewh)), 1);
}

int VisorSurface::create(int w, int h, GLenum f, GLenum t, int count)
{
    checkformat(w, h, f, t, count);

    GLuint curfbo = renderfbo;
    bool restore = false;

    loopi(count)
    {
        int cw = w, ch = h;
        GLenum format = f, target = t;

        switch(i)
        {
            case WORLD:
                gscaledims(cw, ch);
                break;
            case SCALE1: case SCALE2:
            {
                if(!hasglass() && buffers.inrange(i))
                {
                    buffers[i]->cleanup();
                    continue;
                }

                gscaledims(cw, ch, visorglassscale);
                break;
            }
            default: break;
        }

        if(buffers.inrange(i))
        {
            if(buffers[i]->check(cw, ch, format, target))
                restore = true;
            continue;
        }

        buffers.add(new RenderBuffer(cw, ch, format, target));
        restore = true;
    }

    if(restore) glBindFramebuffer_(GL_FRAMEBUFFER, curfbo);

    return buffers.length();
}

bool VisorSurface::check()
{
    return engineready && visorhud;
}

void VisorSurface::coords(float cx, float cy, float &vx, float &vy, bool back)
{
    // WARNING: This function MUST produce the same
    // results as the 'hudvisor' shader for cursor projection.

    vec2 from(cx, cy), to = from;

    to.sub(vec2(0.5f));
    to.mul(vec2(visorscalex, visorscaley));

    float mag = to.magnitude();

    to.mul(1.0 + visordistort * visornormal * visornormal);
    to.div(1.0 + visordistort + mag * mag);

    to.add(vec2(0.5f));
    if(!back) to.sub(from);

    vx = back ? to.x : from.x - to.x; // what we get is an offset from requested position
    vy = back ? to.y : from.y - to.y; // that is then returned or subtracted from it
}

float VisorSurface::getcursorx(int type)
{
    switch(type)
    {
        case -1: return cursorx; // force cursor
        case 1: return config.cursorx; // force visor
        default: break;
    }
    return rendervisor == VISOR ? config.cursorx : cursorx;
}

float VisorSurface::getcursory(int type)
{
    switch(type)
    {
        case -1: return ::cursory; // force cursor
        case 1: return cursory; // force visor
        default: break;
    }
    return rendervisor == VISOR ? cursory : ::cursory;
}

static void setcompositedblend(bool precomposited)
{
    if(precomposited) glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    else glBlendFuncSeparate_(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_ONE);
}

bool VisorSurface::render(int w, int h, GLenum f, GLenum t, int count)
{
    bool noview = hasnoview(), wantvisor = visorsurf.check();

    if(engineready && create(w, h, f, t, count))
    {
        // setup our coordinate system for the visor if we're ok to proceed
        config.reset();

        if(wantvisor) visorsurf.coords(cursorx, cursory, config.cursorx, config.cursory, true);
        else
        {
            config.cursorx = cursorx;
            config.cursory = cursory;
        }

        hud::visorinfo(config, noview);

        if(config.offsetx) config.cursorx += config.offsetx / buffers[0]->width;
        if(config.offsety) config.cursory += config.offsety / buffers[0]->height;

        config.enabled = true;
    }
    else config.clear();

    glEnable(GL_BLEND);

    if(config.enabled)
    {
        // initialise some stuff we use at different stages below
      
        bool wantblur = false; // force the blur for things like the progress screen
        savefbo();

        setcompositedblend(false);

        // render out the various layers so we can composite them together later
        loopi(BUFFERS)
        {
            if((i == WORLD && noview) || !bindfbo(i)) continue;

            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);

            hudmatrix.ortho(0, vieww, viewh, 0, -1, 1);
            flushhudmatrix();
            resethudshader();

            rendervisor = wantvisor ? i : -1;
            switch(i)
            {
                case WORLD:
                {
                    // world UI's use gdepth for depth testing

                    bindgdepth();

                    glEnable(GL_DEPTH_TEST);
                    glDepthMask(GL_FALSE);

                    UI::render(SURFACE_WORLD);

                    glDepthMask(GL_TRUE);
                    glDisable(GL_DEPTH_TEST);

                    break;
                }
                case VISOR:
                {
                    // main visor rendering

                    UI::render(progressing ? SURFACE_PROGRESS : SURFACE_VISOR);
                    hud::visorrender(vieww, viewh, wantvisor, noview);

                    break;
                }
                case HUD:
                {
                    if(noview) drawprogress();
                    else
                    {
                        halosurf.draw();
                        UI::render(SURFACE_BACKGROUND);
                        hud::startrender(vieww, viewh, wantvisor, noview);

                        hudmatrix.ortho(0, vieww, viewh, 0, -1, 1);
                        flushhudmatrix();
                        resethudshader();

                        hudrectshader->set();
                        setcompositedblend(true); // WORLD buffer contains composited content
                        bindtex(WORLD, 0);
                        hudquad(0, 0, vieww, viewh, 0, buffers[WORLD]->height, buffers[WORLD]->width, -buffers[WORLD]->height);
                        setcompositedblend(false);
                    }

                    if(wantvisor)
                    {
                        SETSHADER(hudvisor);
                        LOCALPARAMF(visorsize, vieww, viewh, 1.0f / vieww, 1.0f / viewh);
                        LOCALPARAMF(visorparams, visordistort, visornormal, visorscalex, visorscaley);
                    }
                    else hudrectshader->set();

                    setcompositedblend(true); // VISOR buffer contains composited content
                    bindtex(VISOR, 0);
                    if(!noview && !hud::hasinput(true))
                        hudquad(config.offsetx, config.offsety, vieww, viewh, 0, buffers[VISOR]->height, buffers[VISOR]->width, -buffers[VISOR]->height);
                    else hudquad(0, 0, vieww, viewh, 0, buffers[VISOR]->height, buffers[VISOR]->width, -buffers[VISOR]->height);
                    
                    setcompositedblend(false);

                    if(!progressing) UI::render(SURFACE_FOREGROUND);

                    hud::endrender(vieww, viewh, wantvisor, noview);

                    if(!progressing)
                    {
                        hudmatrix.ortho(0, vieww, viewh, 0, -1, 1);
                        flushhudmatrix();
                        resethudshader();

                        hud::drawpointers(vieww, viewh, getcursorx(), getcursory());
                    }

                    break;
                }
                case BLIT:
                {
                    // this contains our final image of the viewport
                    if(noview) wantblur = drawnoview();
                    else doscale(renderfbo, vieww, viewh);

                    break;
                }
            }

            rendervisor = -1;
        }

        glBlendFunc(GL_ONE, GL_ZERO);

        // create a blurred copy of the BLIT buffer
        if(wantblur || hasglass())
        {
            copy(SCALE1, buffers[BLIT]->fbo, buffers[BLIT]->width, buffers[BLIT]->height);

            int radius = int(ceilf((wantblur ? visorglassradiusload : visorglassradius) * (max(buffers[SCALE1]->width, buffers[SCALE1]->height) / BASERESOLUTION)));
            if(radius)
            {
                float blurweights[MAXBLURRADIUS+1], bluroffsets[MAXBLURRADIUS+1];
                setupblurkernel(radius, blurweights, bluroffsets);

                loopi(2 + 2*(wantblur ? visorglassiterload : visorglassiter))
                {
                    if(!bindfbo(SCALE1 + ((i + 1) % 2))) continue;
                    setblurshader(i % 2, 1, radius, blurweights, bluroffsets, GL_TEXTURE_RECTANGLE);
                    bindtex(SCALE1 + (i % 2), 0);
                    screenquad(vieww, viewh);
                }
            }

            if(wantblur) copy(BLIT, buffers[SCALE1]->fbo, buffers[SCALE1]->width, buffers[SCALE1]->height, true);
        }

        restorefbo();

        hudmatrix.ortho(0, vieww, viewh, 0, -1, 1);
        flushhudmatrix();
        resethudshader();

        GLOBALPARAMF(visortime, lastmillis / 1000.f);

        // final operations on the viewport before overlaying the UI/visor elements
        if(!engineready)
        {
            hudrectshader->set();
            bindtex(BLIT, 0);
        }
        else if(wantblur || !hasglass())
        {
            // want blur or don't have glass turned on
            SETSHADER(hudblit);

            LOCALPARAMF(blitsize, vieww, viewh, 1.0f / vieww, 1.0f / viewh);
            LOCALPARAMF(blitparams,
                    clamp(visorblitchromamin + config.chroma * visorblitchromascale, visorblitchromamin, visorblitchromamax) * max(buffers[BLIT]->width, buffers[BLIT]->height) / BASERESOLUTION,
                        clamp(config.glitch, 0.0f, 1.0f), 1.0f + config.saturate, config.narrow > 0.0f ? 1.0f / config.narrow : (config.narrow < 0.0f ? 0.0f : 1e16f));

            bindtex(BLIT, 0);
        }
        else
        {
            // glass does alpha blurring and other visor effects
            SETSHADER(hudblitglass);
            LOCALPARAMF(time, lastmillis / 1000.f);
            
            LOCALPARAMF(blitsize, vieww, viewh, 1.0f / vieww, 1.0f / viewh);
            LOCALPARAMF(blitparams,
                    clamp(visorblitchromamin + config.chroma * visorblitchromascale, visorblitchromamin, visorblitchromamax) * max(buffers[BLIT]->width, buffers[BLIT]->height) / BASERESOLUTION,
                        clamp(config.glitch, 0.0f, 1.0f), 1.0f + config.saturate, config.narrow > 0.0f ? 1.0f / config.narrow : (config.narrow < 0.0f ? 0.0f : 1e16f));

            LOCALPARAMF(blitglass, visorglassmin, visorglassmax, visorglassmix, config.bluramt);
            LOCALPARAMF(blitscale, buffers[SCALE1]->width / float(buffers[BLIT]->width), buffers[SCALE1]->height / float(buffers[BLIT]->height));
            
            loopi(COUNT) bindtex(START + i, i);
        }
        
        hudquad(0, 0, vieww, viewh, 0, buffers[BLIT]->height, buffers[BLIT]->width, -buffers[BLIT]->height);

        setcompositedblend(true); // HUD buffer contains composited content

        if(visorrender >= (game::focusedent()->isediting() ? 2 : 1) && !hud::hasinput(true))
        {
            SETSHADER(hudrender);
            LOCALPARAMF(time, lastmillis / 1000.f);
            LOCALPARAMF(rendersize, vieww, viewh, 1.0f / vieww, 1.0f / viewh);
            LOCALPARAMF(renderparams,
                    clamp(visorrenderchromamin + config.chroma * visorrenderchromascale, visorrenderchromamin, visorrenderchromamax) * max(buffers[HUD]->width, buffers[HUD]->height) / BASERESOLUTION,
                        clamp(config.glitch, 0.0f, 1.0f), 1.0f + config.saturate, config.narrow > 0.0f ? 1.0f / config.narrow : (config.narrow < 0.0f ? 0.0f : 1e16f));
        }
        else hudrectshader->set();

        bindtex(HUD, 0);
        hudquad(0, 0, vieww, viewh, 0, buffers[HUD]->height, buffers[HUD]->width, -buffers[HUD]->height);
    }
    else
    {
        // failsafe for when we can't render the visor
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        hudmatrix.ortho(0, vieww, viewh, 0, -1, 1);
        flushhudmatrix();
        resethudshader();

        if(noview)
        {
            drawnoview();
            drawprogress();
        }

        hud::startrender(vieww, viewh, false, noview);
        hud::visorrender(vieww, viewh, false, noview);
        hud::endrender(vieww, viewh, false, noview);
    }

    glDisable(GL_BLEND);

    return true;
}

void ViewSurface::checkformat(int &w, int &h, GLenum &f, GLenum &t, int &n)
{
    w = max(int(w > 0 ? w : vieww), 1);
    h = max(int(h > 0 ? h : viewh), 1);
    gscaledims(w, h);
}

bool ViewSurface::destroy()
{
    if(oqstate)
    {
        delete oqstate;
        oqstate = NULL;
    }
    return cleanup();
}

bool ViewSurface::render(int w, int h, GLenum f, GLenum t, int count)
{
    if(!create(w, h, f, t, count)) return false;

    savefbo();
    if(!bindfbo()) 
    {
        popoqstate();
        return false;
    }

    //if(!oqstate) oqstate = new OQState();
    //pushoqstate(oqstate);

    float oldaspect = aspect, oldfovy = fovy, oldfov = curfov, oldnear = nearplane, oldfar = farplane, oldldrscale = ldrscale, oldldrscaleb = ldrscaleb;
    int olddrawtex = drawtex;
    drawtex = texmode;

    physent *oldcamera = camera1;
    static physent cmcamera;
    cmcamera = *camera1;
    cmcamera.reset();
    cmcamera.type = ENT_CAMERA;
    cmcamera.o = worldpos;
    cmcamera.yaw = yaw;
    cmcamera.pitch = pitch;
    cmcamera.roll = roll;
    camera1 = &cmcamera;
    fixfullrange(camera1->yaw, camera1->pitch, camera1->roll);

    aspect = ratio > 0.0f ? ratio : buffers[0]->width / float(buffers[0]->height);
    curfov = fov;
    fovy = 2.0f * atan2(tan(curfov * 0.5f * RAD), aspect) / RAD;

    nearplane = nearpoint;
    farplane = worldsize * farscale;

    projmatrix.perspective(fovy, aspect, nearplane, farplane);
    setcamprojmatrix();
    setviewcell(camera1->o);

    gl_setupframe(true);

    vieww = buffers[0]->width;
    viewh = buffers[0]->height;

    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    gl_drawview();
    copyhdr(buffers[0]->width, buffers[0]->height, buffers[0]->fbo);

    drawtex = olddrawtex;
    ldrscale = oldldrscale;
    ldrscaleb = oldldrscaleb;
    farplane = oldfar;
    nearplane = oldnear;
    aspect = oldaspect;
    fovy = oldfovy;
    curfov = oldfov;
    camera1 = oldcamera;

    projmatrix.perspective(fovy, aspect, nearplane, farplane);
    setcamprojmatrix();
    restorefbo();
    //popoqstate();

    return true;
}
