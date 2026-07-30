#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/types.h"
#include "game/sky.h"
#include "raster/backend.h"

typedef struct {
    const char *name;
    float time_of_day;
    float yaw, pitch;
    int underwater;
    int max_changed_pixels;
    int max_channel_diff;
} SkyCase;

static int compare_case(const SkyCase *tc, int run,
                        const CrFramebuffer *cpu,
                        const CrFramebuffer *metal) {
    int changed_pixels=0,changed_channels=0,max_diff=0,first=-1,first_channel=-1;
    long long sum_diff=0;
    int count=cpu->w*cpu->h;
    for(int i=0;i<count;++i){
        int pixel_changed=0;
        const unsigned char *a=(const unsigned char *)&cpu->color[i];
        const unsigned char *b=(const unsigned char *)&metal->color[i];
        for(int channel=0;channel<4;++channel){
            int diff=abs((int)a[channel]-(int)b[channel]);
            if(diff){
                pixel_changed=1;changed_channels++;sum_diff+=diff;
                if(first<0){first=i;first_channel=channel;}
                if(diff>max_diff)max_diff=diff;
            }
        }
        changed_pixels+=pixel_changed;
        if(memcmp(&cpu->depth[i],&metal->depth[i],sizeof(float))){
            fprintf(stderr,"FAIL sky %-12s run=%d depth changed at (%d,%d)\n",
                    tc->name,run,i%cpu->w,i/cpu->w);
            return 0;
        }
    }
    printf("SKY %-12s run=%d changed_px=%d/%d changed_ch=%d maxdiff=%d sumdiff=%lld",
           tc->name,run,changed_pixels,count,changed_channels,max_diff,sum_diff);
    if(first>=0){
        const unsigned char *a=(const unsigned char *)&cpu->color[first];
        const unsigned char *b=(const unsigned char *)&metal->color[first];
        printf(" first=(%d,%d,c%d) %u/%u",first%cpu->w,first/cpu->w,
               first_channel,a[first_channel],b[first_channel]);
    }
    putchar('\n');
    if(changed_pixels>tc->max_changed_pixels||max_diff>tc->max_channel_diff){
        fprintf(stderr,
                "FAIL sky %-12s tolerance changed_px<=%d maxdiff<=%d; first difference above\n",
                tc->name,tc->max_changed_pixels,tc->max_channel_diff);
        return 0;
    }
    return 1;
}

int main(void){
    enum { WIDTH=257,HEIGHT=259 };
    CrFramebuffer cpu,metal,previous;
    CrRasterBackend *backend=NULL,*cpu_backend=NULL;
    char error[512];
    int ok=1;
    if(!cr_fb_alloc(&cpu,WIDTH,HEIGHT)||!cr_fb_alloc(&metal,WIDTH,HEIGHT)||
       !cr_fb_alloc(&previous,WIDTH,HEIGHT)||
       !cr_backend_open(&backend,GM_BACKEND_METAL,WIDTH,HEIGHT,1,error,sizeof error)){
        fprintf(stderr,"Metal sky setup failed: %s\n",error);return 1;
    }
    SkyCase cases[]={
        {"clear-noon",0.25f,0.31f,-0.17f,0,0,0},
        /* One 1-LSB channel at a sky-plane sqrt rounding boundary. */
        {"sunset",0.49f,-1.2f,0.08f,0,1,1},
        /* Device sin in hash21 moves isolated star dots. Measured M4:
         * 6/66,563 pixels (0.0091%), 18 channels, max 117. */
        {"midnight",0.75f,0.77f,-0.22f,0,8,128},
        {"underwater",0.25f,-0.41f,0.19f,1,0,0},
    };
    for(unsigned ci=0;ci<sizeof cases/sizeof cases[0];++ci){
        const SkyCase *tc=&cases[ci];
        CrCamera cam={{1.0f,65.0f,-2.0f},tc->yaw,tc->pitch,70.0f,
                      (float)WIDTH/(float)HEIGHT,0.05f,128.0f,0.0f,0.0f};
        gm_sky_set_fog_c1(1.0f);gm_sky_set_eye_height(1.62f);
        gm_sky_set_fluid_fog(tc->underwater,
                             (CrVec3){0.08f,0.19f,0.31f},0.1f);
        for(int run=0;run<2;++run){
            CrRgba clear={(u8)(13+run),27,41,255};
            GmSkyCtx sky;float basis[11];
            cr_fb_clear(&cpu,clear);cr_fb_clear(&metal,clear);
            gm_sky_draw(&cpu,&cam,tc->time_of_day);
            gm_sky_frame_args(&cam,tc->time_of_day,&sky,basis);
            if(!cr_backend_frame_begin(backend,&metal)||
               !cr_backend_sky(backend,&sky,basis,WIDTH,HEIGHT)||
               !cr_backend_frame_end(backend,&metal)){
                fprintf(stderr,"Metal sky %s failed: %s\n",tc->name,
                        cr_backend_last_error(backend));ok=0;break;
            }
            if(!compare_case(tc,run,&cpu,&metal))ok=0;
            if(run==0)memcpy(previous.color,metal.color,
                             (size_t)WIDTH*HEIGHT*sizeof *metal.color);
            else if(memcmp(previous.color,metal.color,
                           (size_t)WIDTH*HEIGHT*sizeof *metal.color)){
                fprintf(stderr,"FAIL sky %s repeated Metal output changed\n",tc->name);
                ok=0;
            }
        }
    }
    gm_sky_set_fluid_fog(0,(CrVec3){0,0,0},0);

    if(!cr_backend_open(&cpu_backend,GM_BACKEND_CPU,WIDTH,HEIGHT,1,
                        error,sizeof error)||
       cr_backend_sky(cpu_backend,(const GmSkyCtx *)(uintptr_t)1,
                      (const float *)(uintptr_t)1,WIDTH,HEIGHT)||
       strstr(cr_backend_last_error(cpu_backend),"does not implement GPU sky")==NULL){
        fprintf(stderr,"FAIL CPU sky capability error path: %s\n",
                cpu_backend?cr_backend_last_error(cpu_backend):error);ok=0;
    }else puts("PASS CPU sky capability error path");

    cr_backend_close(cpu_backend);cr_backend_close(backend);
    cr_fb_free(&cpu);cr_fb_free(&metal);cr_fb_free(&previous);
    puts(ok?"METAL SKY PARITY PASS":"METAL SKY PARITY FAIL");
    return ok?0:1;
}
