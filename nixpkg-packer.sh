#!/usr/bin/env bash
set -e

if [ "$EUID" -ne 0 ]; then
  echo "Please run the packer as root (sudo ./nixpak-packer.sh)"
  exit 1
fi

echo "=> [1/4] Packing Core Engine (nixpkg-core.c)..."
cat << 'EOF' > nixpkg-core.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>
#define CFG "/etc/nixos/nixpkg-apps.nix"
int rws(const char*c){FILE*fp=popen(c,"r");if(!fp)return 1;char b[1024];while(fgets(b,1024,fp))printf("%s",b);return WEXITSTATUS(pclose(fp));}
bool ifp(const char*n){return strncmp(n,"flatpak:",8)==0;}
int mp(const char*a,const char*n){if(ifp(n)){char c[512];snprintf(c,512,"PATH=$PATH:/run/current-system/sw/bin flatpak %s -y flathub %s 2>&1",strcmp(a,"install")==0?"install":"uninstall",n+8);return rws(c);}
FILE*fp=fopen(CFG,"r");if(!fp)return 1;char*l[1000];int lc=0,si=-1;bool fd=false;char b[256];
while(fgets(b,256,fp)&&lc<1000){if(strstr(b,"# NIXPKG_START"))si=lc;char t[256]={0};if(sscanf(b," %255s ",t)==1&&strcmp(t,n)==0){fd=true;if(strcmp(a,"remove")==0)continue;}l[lc++]=strdup(b);}fclose(fp);
if(strcmp(a,"install")==0&&fd)return 0;if(strcmp(a,"remove")==0&&!fd)return 1;
fp=fopen(CFG,"w");for(int i=0;i<lc;i++){fprintf(fp,"%s",l[i]);if(strcmp(a,"install")==0&&i==si)fprintf(fp,"    %s\n",n);free(l[i]);}fclose(fp);return 0;}
int main(int C,char**V){if(C<2)return 1;if(!strcmp(V[1],"gc"))return rws("PATH=$PATH:/run/current-system/sw/bin nix-env --delete-generations old -p /nix/var/nix/profiles/system 2>&1&&PATH=$PATH:/run/current-system/sw/bin nix-collect-garbage -d 2>&1");
if(!strcmp(V[1],"test"))return rws("PATH=$PATH:/run/current-system/sw/bin nixos-rebuild test 2>&1");if(!strcmp(V[1],"switch"))return rws("PATH=$PATH:/run/current-system/sw/bin nixos-rebuild switch 2>&1");
if(!strcmp(V[1],"rollback"))return rws("PATH=$PATH:/run/current-system/sw/bin nixos-rebuild switch --rollback 2>&1");if(C<3)return 1;return mp(V[1],V[2]);}
EOF

echo "=> [2/4] Packing Python Data Broker (nixpak-backend.py)..."
cat << 'EOF' > nixpak-backend.sh
#!/bin/sh
export PATH=/run/current-system/sw/bin:/usr/bin:/bin:$PATH
export NIXPAK_ACTION="$1";export NIXPAK_ARG1="$2";export NIXPAK_ARG2="$3"
P="";for py in python3.15 python3.14 python3 python;do if command -v $py >/dev/null 2>&1;then P=$py;break;fi;done
if [ -n "$P" ];then exec $P /usr/local/bin/nixpak-backend.py;else exec nix-shell -p python3 --run "python3 /usr/local/bin/nixpak-backend.py";fi
EOF

cat << 'EOF' > nixpak-backend.py
import json,urllib.request as R,os,sys,subprocess as S,re
os.environ['PATH']+=':/run/current-system/sw/bin:/usr/bin:/bin'
A,A1,A2=os.environ.get('NIXPAK_ACTION',''),os.environ.get('NIXPAK_ARG1',''),os.environ.get('NIXPAK_ARG2','');SM='relevance'
try:
 with open(os.path.expanduser('~/.config/nixpak/config'),'r')as f:
  for l in f:
   if l.startswith("SEARCH_MODE="):SM=l.strip().split('=')[1]
except:pass
def rc(c):
 try:return S.check_output(c,stderr=S.DEVNULL,text=True,timeout=8)
 except:return ""
def fj(u,d=None):
 q=R.Request(u,data=d,headers={'User-Agent':'M'})
 if d:q.add_header('Content-Type','application/json')
 try:
  with R.urlopen(q,timeout=4)as r:return json.loads(r.read())
 except:return {}
def sz(t):return str(t).replace('\n',' ').replace('|','-').strip() if t else ""
def im(q,n,a):return True if SM=='relevance' else(q.lower() in n.lower() or q.lower() in a.lower())
if A=="search":
 q,sn=A1,set();no=rc(['nix','--extra-experimental-features','nix-command flakes','search','--json','nixpkgs',q])
 if no:
  try:
   for a,i in list(json.loads(no).items())[:60]:
    n,ca=a.split('.')[-1],a.replace('legacyPackages.x86_64-linux.','')
    if im(q,n,ca)and ca not in sn:print(f"NIX|{sz(n)}|{sz(i.get('description',''))}|{sz(ca)}");sys.stdout.flush();sn.add(ca)
  except:pass
 else:
  eo=rc(['nix-env','-qa',f'.*{q}.*','--json'])
  if eo:
   try:
    for a,i in list(json.loads(eo).items())[:60]:
     n=i.get('pname',i.get('name',a.split('-')[0]))
     if im(q,n,a)and a not in sn:print(f"NIX|{sz(n)}|{sz(i.get('meta',{}).get('description',''))}|{sz(a)}");sys.stdout.flush();sn.add(a)
   except:pass
 for h in fj('https://flathub.org/api/v2/search',json.dumps({"query":q}).encode()).get('hits',[])[:40]:
  a,n,ic=h.get('app_id',''),h.get('name',''),h.get('icon','')
  if ic and not ic.startswith('http'):ic=f"https://dl.flathub.org{ic}"
  if im(q,n,a)and a not in sn:print(f"FLATPAK|{sz(n)}|{sz(h.get('summary',''))}|{sz(a)}|{sz(ic)}");sys.stdout.flush();sn.add(a)
elif A=="details":
 si=A1.replace('/','_')
 os.makedirs('/tmp/nixpkg/metadata',exist_ok=True)
 os.makedirs('/tmp/nixpkg/img',exist_ok=True)
 om=f'/tmp/nixpkg/metadata/{si}_meta.txt'
 dc,lc,hm,ic,sr,vr,s,ss,sd,ch,al,dv,bg,hl,tr,ig="","Unknown","Unknown","","","Rolling","Variable","System Level","Runs with host privileges","Rolling release.","NONE","Unknown Developer","","","","True"
 sn=A1.split('.')[-1].lower()
 if A2=="NIX":
  mo=rc(['nix','eval','--json',f'nixpkgs#{A1}.meta']) or rc(['nix-env','-qaP','-A',f'nixos.{A1}','--json']) or rc(['nix-env','-qaP','-A',f'nixpkgs.{A1}','--json'])
  if mo:
   try:
    m=json.loads(mo)if'eval'in mo else list(json.loads(mo).values())[0].get('meta',{})
    dc,vr,hm=m.get('description',''),m.get('version','Rolling'),m.get('homepage','');hm=hm[0]if isinstance(hm,list)and hm else str(hm)
    l=m.get('license',{});lc=l.get('fullName',l.get('shortName','Unknown'))if isinstance(l,dict)else(l[0].get('fullName',str(l[0]))if isinstance(l,list)and l else str(l))
    mt=m.get('maintainers',[]);dv=mt[0].get('name','NixOS Community')if mt and isinstance(mt,list)and isinstance(mt[0],dict)else'NixOS Community'
    if any(x in dc.lower()for x in["cli ","command line","terminal"]):ig="False"
   except:pass
  else:dc,lc,hm,dv="Native NixOS package.","Nixpkgs","https://search.nixos.org","NixOS Community"
  try:
   hs=fj('https://flathub.org/api/v2/search',json.dumps({"query":sn}).encode()).get('hits',[])
   if any(sn in str(h.get('name','')).lower()or sn in str(h.get('app_id','')).lower()for h in hs):al="FLATPAK"
   if hs:
    st=fj(f"https://flathub.org/api/v2/appstream/{hs[0].get('app_id')}");ic=st.get('icon','')
    if ic and not ic.startswith('http'):ic=f"https://dl.flathub.org{ic}"
    srs=st.get('screenshots',[]);sr=srs[0].get('thumbnails',[{}])[0].get('url',srs[0].get('sourceImage',{}).get('url',''))if srs else""
  except:pass
 elif A2=="FLATPAK":
  st=fj(f'https://flathub.org/api/v2/appstream/{A1}')
  dc,lc,dv=st.get('summary',''),st.get('project_license','Unknown'),st.get('developer_name','Unknown Developer')
  if "console-application"in str(st.get('type','')).lower():ig="False"
  u=st.get('urls',{});hm,bg,hl,tr=u.get('homepage',''),u.get('bugtracker',''),u.get('help',''),u.get('translate','')
  dsz=st.get('bundle',{}).get('runtime_size',0);s=f"{dsz/(1024*1024):.1f} MB"if dsz else s
  ss,sd="Potentially Unsafe","Can access system files/services";rls=st.get('releases',[])
  if rls:
   vr,rl=rls[0].get('version','Unknown'),rls[0].get('description','')
   if rl:ch=re.sub(' +',' ',re.sub('<[^<]+>',' ',rl)).strip()
  if rc(['nix-env','-qa',f'.*{sn}.*','--json'])!='{}':al="NIXOS"
  ic=st.get('icon','');ic=f"https://dl.flathub.org{ic}"if ic and not ic.startswith('http')else ic
  srs=st.get('screenshots',[]);sr=srs[0].get('thumbnails',[{}])[0].get('url',srs[0].get('sourceImage',{}).get('url',''))if srs else""
 if ic and not os.path.exists(f'/tmp/nixpkg/img/{si}_icon.png'):S.run(['curl','-sLA','M',ic,'-o',f'/tmp/nixpkg/img/{si}_icon.png'])
 if sr and not os.path.exists(f'/tmp/nixpkg/img/{si}_screen.png'):S.run(['curl','-sLA','M',sr,'-o',f'/tmp/nixpkg/img/{si}_screen.png'])
 with open(om,'w')as f:f.write(f"{sz(dc)[:500]}\n{sz(lc)[:100]}\n{sz(hm)[:200]}\n{sz(vr)[:100]}\n{sz(s)[:50]}\n{sz(ss)[:50]}\n{sz(sd)[:100]}\nAll\n{sz(ch)[:2000]}\n{sz(al)[:50]}\n{sz(dv)[:100]}\n{sz(bg)[:200]}\n{sz(hl)[:200]}\n{sz(tr)[:200]}\n{ig}\n")
EOF

echo "=> [3/4] Packing GTK4 Hypervisor UI (nixpkg-gui.c)..."
cat << 'EOF' > nixpkg-gui.c
#include <gtk/gtk.h>
#include <adwaita.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <sys/stat.h>

GtkWidget *main_stack, *view_stack, *page_explore, *page_system, *page_search, *page_progress, *page_details, *page_settings;
GtkWidget *results_list = NULL, *gens_list, *log_view, *search_entry, *progress_bar, *btn_back;
GtkWidget *details_title, *details_dev_lbl, *details_desc, *details_install_btn, *details_try_btn, *details_icon_img;
GtkWidget *details_screenshot_box, *details_screenshot_img, *details_store_lbl, *details_nix_code, *details_provenance_box, *details_prov_attr;
GtkWidget *prov_row_attr, *prov_row_arch, *prov_row_store, *prov_row_license, *prov_row_home;
GtkWidget *bento_size_badge, *bento_size_title, *bento_size_sub, *bento_safe_icon, *bento_safe_title, *bento_safe_sub;
GtkWidget *bento_ver_title, *bento_change_lbl, *bento_alt_icon, *bento_alt_title, *bento_alt_sub;
GtkWidget *tile_1, *tile_2, *tile_3, *tile_4, *tile_alt, *version_card;
GtkWidget *comm_card_icon1, *comm_card_icon2, *comm_card_icon3, *comm_card_title, *comm_card_desc, *comm_card_license;
GtkWidget *link_web_row, *link_bug_row, *link_help_row, *link_trans_row, *sys_stats_label;

int cfg_show_size = 1, cfg_show_safe = 1, cfg_show_desk = 1, cfg_show_age = 1;
char cfg_search_mode[32] = "relevance";
GtkWidget *sw_size, *sw_safe, *sw_desk, *sw_age, *sw_literal;
static GQueue *pending_results_queue = NULL;

typedef struct { gchar *type; gchar *name; gchar *desc; gchar *app_id; gchar *icon_url; GtkWidget *row_icon_widget; } AppResult;
typedef struct { char *query; int action; } TaskData;
typedef struct { gchar *app_id; gchar *type; GtkWidget *icon_widget; } MediaTask; 

void free_app_result(gpointer data);
void load_config(void);
void save_config(void);
static void on_setting_changed(GObject *obj, GParamSpec *pspec, gpointer data);
void get_system_stats(char *buf, size_t size);
gboolean update_stats_ui(gpointer d);
gboolean check_installed(const char *app_id, const char *type);
const char *get_fallback_icon(const char *app_id);
static void on_settings_clicked(GtkWidget *w, gpointer d);
static void on_back_clicked(GtkWidget *w, gpointer d);
static void on_link_clicked(GtkListBoxRow *row, gpointer user_data);
static void load_more_results(int count);
static void on_scroll_edge_reached(GtkScrolledWindow *sw, GtkPositionType pos, gpointer data);
gboolean idle_initial_batch(gpointer data);
GtkWidget* create_rich_row(const char *icon_name, const char *title, const char *subtitle, const char *css_class);
static void bento_tile_clicked(GtkButton *btn, gpointer data);
gboolean apply_media(gpointer data);
gpointer fetch_media_thread(gpointer data);
static void on_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer user_data);
static void trigger_try_shell(GtkWidget *btn_widget);
void add_result_card(AppResult *res);
gboolean add_generation_row(gpointer data);
gboolean pulse_progress(gpointer d);
gboolean append_log(gpointer data);
gboolean finish_task(gpointer d);
gpointer background_task(gpointer data);
static void on_gc_response(GObject *source_object, GAsyncResult *res, gpointer user_data);
static void trigger_gc_confirm(GtkWidget *btn_widget);
static void trigger_system_action(GtkWidget *btn_widget);
static void trigger_action(GtkWidget *btn_widget);
static void on_search_changed(GtkSearchEntry *entry, gpointer d);
static void on_search_activated(GtkWidget *entry, gpointer d);
static void on_card_clicked(GtkWidget *w, gpointer data);
GtkWidget* create_color_btn(const char *icon_name, const char *label, const char *css_name, const char *info_text);
GtkWidget* create_action_btn(const char *label, int action_code);
GtkWidget* create_bento_button(GtkWidget *top_widget, GtkWidget **title_lbl, GtkWidget **sub_lbl);
GtkWidget* create_horizontal_bento_button(GtkWidget *left_widget, GtkWidget **title_lbl, GtkWidget **sub_lbl);
static void activate(GtkApplication *app, gpointer user_data);

void free_app_result(gpointer data) {
    AppResult *res = (AppResult*)data;
    if (!res) return;
    g_free(res->type); g_free(res->name); g_free(res->desc); g_free(res->app_id); 
    if (res->icon_url) g_free(res->icon_url);
    g_free(res);
}

void load_config() {
    const char *home = getenv("HOME");
    if (!home) return;
    char path[1024]; snprintf(path, sizeof(path), "%s/.config/nixpak", home);
    char cmd[2048]; snprintf(cmd, sizeof(cmd), "mkdir -p \"%s\"", path); int ret = system(cmd); (void)ret;
    strcat(path, "/config"); FILE *f = fopen(path, "r"); if (!f) return;
    char line[256];
    while(fgets(line, sizeof(line), f)) {
        if (strncmp(line, "SHOW_SIZE=", 10)==0) cfg_show_size = atoi(line+10);
        if (strncmp(line, "SHOW_SAFE=", 10)==0) cfg_show_safe = atoi(line+10);
        if (strncmp(line, "SHOW_DESK=", 10)==0) cfg_show_desk = atoi(line+10);
        if (strncmp(line, "SHOW_AGE=", 9)==0)  cfg_show_age = atoi(line+9);
        if (strncmp(line, "SEARCH_MODE=", 12)==0) sscanf(line+12, "%31s", cfg_search_mode);
    }
    fclose(f);
}

void save_config() {
    char path[1024]; snprintf(path, sizeof(path), "%s/.config/nixpak/config", getenv("HOME"));
    FILE *f = fopen(path, "w");
    if(f) {
        fprintf(f, "SHOW_SIZE=%d\nSHOW_SAFE=%d\nSHOW_DESK=%d\nSHOW_AGE=%d\nSEARCH_MODE=%s\n", cfg_show_size, cfg_show_safe, cfg_show_desk, cfg_show_age, cfg_search_mode);
        fclose(f);
    }
}

static void on_setting_changed(GObject *obj, GParamSpec *pspec, gpointer data) {
    cfg_show_size = gtk_switch_get_active(GTK_SWITCH(sw_size)); cfg_show_safe = gtk_switch_get_active(GTK_SWITCH(sw_safe));
    cfg_show_desk = gtk_switch_get_active(GTK_SWITCH(sw_desk)); cfg_show_age = gtk_switch_get_active(GTK_SWITCH(sw_age));
    strcpy(cfg_search_mode, gtk_switch_get_active(GTK_SWITCH(sw_literal)) ? "literal" : "relevance");
    save_config();
    if (tile_1) gtk_widget_set_visible(tile_1, cfg_show_size);
    if (tile_2) gtk_widget_set_visible(tile_2, cfg_show_safe);
    if (tile_3) gtk_widget_set_visible(tile_3, cfg_show_desk);
    if (tile_4) gtk_widget_set_visible(tile_4, cfg_show_age);
}

const gchar *css_style = 
    ".category-btn { border-radius: 12px; font-weight: bold; font-size: 16px; margin: 5px; min-height: 120px; }"
    ".cat-create { background-image: linear-gradient(135deg, #3584e4, #9141ac); color: white; border: none; }"
    ".cat-work { background-color: #f6d32d; background-image: linear-gradient(rgba(0,0,0,0.1) 1px, transparent 1px), linear-gradient(90deg, rgba(0,0,0,0.1) 1px, transparent 1px); background-size: 10px 10px; color: #1a5fb4; border: none; }"
    ".cat-play { background-image: linear-gradient(135deg, #e01b24, #c061cb); color: white; border: none; }"
    ".cat-social { background-image: linear-gradient(135deg, #ed333b, #ff7800); color: white; border: none; }"
    ".cat-learn { background-image: linear-gradient(135deg, #26a269, #33d17a); color: white; border: none; }"
    ".cat-develop { background-image: linear-gradient(135deg, #3d3846, #77767b); color: white; border: none; }"
    ".screenshot-box { background-color: #111111; border-radius: 12px; min-height: 400px; margin-bottom: 24px; }"
    ".code-box { background-color: #1e1e1e; color: #c0c0c0; padding: 12px; border-radius: 8px; font-family: monospace; font-size: 12px; margin-top: 12px; }"
    ".pkg-nix { border-left: 4px solid #5fb8f2; background: linear-gradient(90deg, rgba(95,184,242,0.1) 0%, transparent 50px); }"
    ".pkg-flatpak { border-left: 4px solid #385970; background: linear-gradient(90deg, rgba(56,89,112,0.15) 0%, transparent 50px); }"
    ".bento-tile { background-color: alpha(currentColor, 0.05); border-radius: 16px; padding: 20px; }"
    ".bento-tile:hover { background-color: alpha(currentColor, 0.08); }"
    ".bento-badge { border-radius: 9999px; padding: 6px 16px; font-weight: bold; font-size: 16px; }"
    ".badge-gray { background-color: alpha(currentColor, 0.1); }"
    ".badge-safe { background-color: alpha(#2ec27e, 0.2); color: #2ec27e; }"
    ".safe-color { color: #2ec27e; }"
    ".warning-color { color: #e66100; }"
    ".comm-icon { background-color: alpha(#2ec27e, 0.15); color: #2ec27e; border-radius: 999px; padding: 8px; }"
    ".comm-icon-warn { background-color: alpha(#e66100, 0.15); color: #e66100; border-radius: 999px; padding: 8px; }"
    ".icon-safe { color: #2ec27e; background-color: alpha(#2ec27e, 0.15); border-radius: 999px; padding: 8px; margin-right: 12px; }"
    ".icon-warn { color: #e66100; background-color: alpha(#e66100, 0.15); border-radius: 999px; padding: 8px; margin-right: 12px; }";

const char *header_ascii_logo = 
    "<span font_family='monospace' font_size='2.5pt' line_height='0.9'>"
    "<span foreground='#FFFFFF'>█</span><span foreground='#FFFFFF' background='#AAAAAA'>▀▀▀▀▀▀▀▀▀▀▄▄</span><span foreground='#555555' background='#AAAAAA'>▀</span><span foreground='#AAAAAA'>▄</span><span foreground='#555555'>▄</span><span foreground='#AAAAAA'>    </span>\n"
    "<span foreground='#FFFFFF'>█</span><span foreground='#AAAAAA'>█      </span><span foreground='#55FFFF'>■</span><span foreground='#AAAAAA'> </span><span foreground='#55FFFF'>▄▄</span><span foreground='#AAAAAA'> </span><span foreground='#FFFFFF'>▀</span><span foreground='#FFFFFF' background='#AAAAAA'>▄</span><span foreground='#AAAAAA'>█</span><span foreground='#555555' background='#AAAAAA'>▀</span><span foreground='#555555'>▄</span><span foreground='#AAAAAA'>  </span>\n"
    "<span foreground='#FFFFFF'>█</span><span foreground='#AAAAAA'>█ </span><span foreground='#0000AA'>░</span><span foreground='#AAAAAA'>        </span><span foreground='#55FFFF'>▀▄</span><span foreground='#AAAAAA'> </span><span foreground='#FFFFFF'>█</span><span foreground='#AAAAAA'>█</span><span foreground='#555555' background='#AAAAAA'>▀</span><span foreground='#555555'>▄</span><span foreground='#AAAAAA'> </span>\n"
    "<span foreground='#FFFFFF'>█</span><span foreground='#AAAAAA'>█</span><span foreground='#0000AA'>▒▒░░</span><span foreground='#AAAAAA'> </span><span foreground='#FFFFFF'>▄</span><span foreground='#FFFFFF' background='#AAAAAA'>▀</span><span foreground='#FFFFFF'>▄</span><span foreground='#AAAAAA'>▄ </span><span foreground='#0000AA'>░</span><span foreground='#AAAAAA'> </span><span foreground='#55FFFF'>▌</span><span foreground='#FFFFFF'>▐</span><span foreground='#FFFFFF' background='#AAAAAA'>▌</span><span foreground='#AAAAAA'>█</span><span foreground='#555555' background='#AAAAAA'>▐</span><span foreground='#555555'>▌</span>\n"
    "<span foreground='#FFFFFF'>█</span><span foreground='#AAAAAA'>█</span><span foreground='#0000AA'>▓▓▓▓</span><span foreground='#FFFFFF'>█</span><span foreground='#AAAAAA'>██</span><span foreground='#555555'>█</span><span foreground='#FFFFFF'>█</span><span foreground='#0000AA'>▒</span><span foreground='#5555FF' background='#0000AA'>░</span><span foreground='#0000AA'>▓▒░</span><span foreground='#FFFFFF'>█</span><span foreground='#AAAAAA'>██</span><span foreground='#555555'>█</span>\n"
    "<span foreground='#FFFFFF'>█</span><span foreground='#AAAAAA'>█</span><span foreground='#0000AA'>█</span><span foreground='#5555FF' background='#0000AA'>░░</span><span foreground='#0000AA'>█</span><span foreground='#FFFFFF'>█</span><span foreground='#AAAAAA'>██</span><span foreground='#555555'>█</span><span foreground='#FFFFFF'>█</span><span foreground='#5555FF' background='#0000AA'>░▒░</span><span foreground='#0000AA'>█▓</span><span foreground='#FFFFFF'>█</span><span foreground='#AAAAAA'>██</span><span foreground='#555555'>█</span>\n"
    "<span foreground='#FFFFFF'>█</span><span foreground='#AAAAAA'>█</span><span foreground='#5555FF' background='#0000AA'>░░▒▓</span><span foreground='#FFFFFF'>█</span><span foreground='#AAAAAA'>█</span><span foreground='#FFFFFF' background='#AAAAAA'> </span><span foreground='#555555'>█</span><span foreground='#FFFFFF'>█</span><span foreground='#5555FF' background='#0000AA'>░</span><span foreground='#5555FF'>██</span><span foreground='#0000AA'>██</span><span foreground='#FFFFFF'>█</span><span foreground='#FFFFFF' background='#AAAAAA'> </span><span foreground='#AAAAAA'>█</span><span foreground='#555555'>█</span>\n"
    "<span foreground='#FFFFFF' background='#AA0000'>█</span><span foreground='#AAAAAA'>█</span><span foreground='#5555FF' background='#0000AA'>░░▒▓</span><span foreground='#FFFFFF'>█</span><span foreground='#AAAAAA'>██</span><span foreground='#555555'>█</span><span foreground='#FFFFFF'>█</span><span foreground='#5555FF' background='#0000AA'>▒</span><span foreground='#5555FF'>█▓</span><span foreground='#5555FF' background='#0000AA'>▒</span><span foreground='#0000AA'>█</span><span foreground='#FFFFFF'>█</span><span foreground='#AAAAAA'>██</span><span foreground='#555555'>█</span>\n"
    "<span foreground='#FFFFFF' background='#AA0000'>█</span><span foreground='#AAAAAA'>█</span><span foreground='#5555FF' background='#0000AA'>░▒▓▒</span><span foreground='#FFFFFF'>█</span><span foreground='#AAAAAA'>██</span><span foreground='#555555'>█</span><span foreground='#FFFFFF'>█</span><span foreground='#5555FF' background='#0000AA'>▓█▓▒░</span><span foreground='#FFFFFF'>█</span><span foreground='#AAAAAA'>██</span><span foreground='#555555'>█</span>\n"
    "<span foreground='#FFFFFF' background='#AA0000'>█</span><span foreground='#AAAAAA'>█</span><span foreground='#5555FF' background='#0000AA'>░▒▓▓</span><span foreground='#FFFFFF'>█</span><span foreground='#AAAAAA'>██</span><span foreground='#555555'>█</span><span foreground='#FFFFFF'>█</span><span foreground='#5555FF' background='#0000AA'>░▒░</span><span foreground='#0000AA'>█░</span><span foreground='#FFFFFF'>█</span><span foreground='#AAAAAA'>██</span><span foreground='#555555'>█</span>\n"
    "<span foreground='#FFFFFF' background='#AA0000'>█</span><span foreground='#FFFFFF' background='#AAAAAA'>▄</span><span foreground='#FFFFFF' background='#0000AA'>▄▄▄▄</span><span foreground='#FFFFFF'>█</span><span foreground='#AAAAAA'>██</span><span foreground='#555555'>█</span><span foreground='#FFFFFF'>█</span><span foreground='#FFFFFF' background='#0000AA'>▄▄▄▄</span><span foreground='#FFFFFF'>▄█</span><span foreground='#AAAAAA'>██</span><span foreground='#555555'>█</span>"
    "</span>";

void get_system_stats(char *buf, size_t size) {
    FILE *fp = popen("PATH=$PATH:/run/current-system/sw/bin ls -d /nix/var/nix/profiles/system-*-link 2>/dev/null | wc -l", "r");
    char gens[16] = "?"; if (fp) { if (fgets(gens, sizeof(gens), fp) != NULL) gens[strcspn(gens, "\n")] = 0; pclose(fp); }
    fp = popen("PATH=$PATH:/run/current-system/sw/bin df -h /nix | awk 'NR==2 {print $3}'", "r");
    char store_size[16] = "?"; if (fp) { if (fgets(store_size, sizeof(store_size), fp) != NULL) store_size[strcspn(store_size, "\n")] = 0; pclose(fp); }
    snprintf(buf, size, "Generation <b>%s</b> · /nix/store <b>%s</b>", gens, store_size);
}

gboolean update_stats_ui(gpointer d) {
    char sys_stats[256]; get_system_stats(sys_stats, sizeof(sys_stats));
    gtk_label_set_markup(GTK_LABEL(sys_stats_label), sys_stats);
    return G_SOURCE_REMOVE;
}

gboolean check_installed(const char *app_id, const char *type) {
    char cmd[1024];
    if (strcmp(type, "FLATPAK") == 0) {
        gchar *quoted_id = g_shell_quote(app_id);
        snprintf(cmd, sizeof(cmd), "PATH=$PATH:/run/current-system/sw/bin flatpak list --app --columns=application | grep -q '^%s$'", quoted_id);
        g_free(quoted_id);
    } else { snprintf(cmd, sizeof(cmd), "grep -q '^[ \t]*%s$' /etc/nixos/nixpkg-apps.nix", app_id); }
    if (system(cmd) == -1) return FALSE;
    return (system(cmd) == 0);
}

const char *get_fallback_icon(const char *app_id) {
    if (strstr(app_id, "firefox") || strstr(app_id, "browser")) return "web-browser-symbolic";
    if (strstr(app_id, "code") || strstr(app_id, "vim") || strstr(app_id, "git")) return "applications-development-symbolic";
    if (strstr(app_id, "game") || strstr(app_id, "steam")) return "applications-games-symbolic";
    if (strstr(app_id, "office") || strstr(app_id, "libreoffice") || strstr(app_id, "collabora")) return "applications-office-symbolic";
    if (strstr(app_id, "audio") || strstr(app_id, "video") || strstr(app_id, "media")) return "applications-multimedia-symbolic";
    return "application-x-executable-symbolic";
}

static void on_settings_clicked(GtkWidget *w, gpointer d) {
    gtk_stack_set_visible_child_name(GTK_STACK(main_stack), "settings"); gtk_widget_set_visible(btn_back, TRUE);
}

static void on_back_clicked(GtkWidget *w, gpointer d) {
    const char *visible = gtk_stack_get_visible_child_name(GTK_STACK(main_stack));
    if (g_strcmp0(visible, "details") == 0 || g_strcmp0(visible, "progress") == 0 || g_strcmp0(visible, "settings") == 0) {
        if (strlen(gtk_editable_get_text(GTK_EDITABLE(search_entry))) > 0) gtk_stack_set_visible_child_name(GTK_STACK(main_stack), "search");
        else { gtk_stack_set_visible_child_name(GTK_STACK(main_stack), "view_stack"); gtk_widget_set_visible(btn_back, FALSE); }
    }
}

static void on_link_clicked(GtkListBoxRow *row, gpointer user_data) {
    const char *url = g_object_get_data(G_OBJECT(row), "url");
    if (url && strlen(url) > 0 && strcmp(url, "Not available") != 0) {
        char cmd[1024]; snprintf(cmd, sizeof(cmd), "xdg-open '%s'", url); g_spawn_command_line_async(cmd, NULL);
    }
}

static void load_more_results(int count) {
    if (!pending_results_queue) return;
    int loaded = 0;
    while (!g_queue_is_empty(pending_results_queue) && loaded < count) {
        AppResult *res = (AppResult*)g_queue_pop_head(pending_results_queue); add_result_card(res); loaded++;
    }
}

static void on_scroll_edge_reached(GtkScrolledWindow *sw, GtkPositionType pos, gpointer data) {
    if (pos == GTK_POS_BOTTOM) load_more_results(10);
}

gboolean idle_initial_batch(gpointer data) { load_more_results(12); return G_SOURCE_REMOVE; }

GtkWidget* create_rich_row(const char *icon_name, const char *title, const char *subtitle, const char *css_class) {
    GtkWidget *row = adw_action_row_new(); adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    if (subtitle) adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);
    GtkWidget *icon = gtk_image_new_from_icon_name(icon_name); if (css_class) gtk_widget_add_css_class(icon, css_class);
    adw_action_row_add_prefix(ADW_ACTION_ROW(row), icon); return row;
}

static void bento_tile_clicked(GtkButton *btn, gpointer data) {
    const char *tile_id = g_object_get_data(G_OBJECT(btn), "tile_id");
    const char *main_val = g_object_get_data(G_OBJECT(btn), "dialog_main_val");
    const char *sub_val = g_object_get_data(G_OBJECT(btn), "dialog_sub_val");
    if (!tile_id) return;

    AdwDialog *dialog = adw_dialog_new(); adw_dialog_set_content_width(ADW_DIALOG(dialog), 480); adw_dialog_set_content_height(ADW_DIALOG(dialog), 550);
    GtkWidget *view = adw_toolbar_view_new(); GtkWidget *hb = adw_header_bar_new(); adw_header_bar_set_show_title(ADW_HEADER_BAR(hb), FALSE); adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(view), hb);
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 24); gtk_widget_set_margin_top(content, 20); gtk_widget_set_margin_bottom(content, 24); gtk_widget_set_margin_start(content, 28); gtk_widget_set_margin_end(content, 28);
    GtkWidget *hero_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12); gtk_widget_set_halign(hero_box, GTK_ALIGN_CENTER); gtk_box_append(GTK_BOX(content), hero_box);
    GtkWidget *list = gtk_list_box_new(); gtk_widget_add_css_class(list, "boxed-list"); gtk_box_append(GTK_BOX(content), list);

    if (g_strcmp0(tile_id, "SIZE") == 0) {
        GtkWidget *hero_lbl = gtk_label_new(NULL); gtk_label_set_markup(GTK_LABEL(hero_lbl), g_strdup_printf("<span font_size='xx-large' font_weight='bold'>%s</span>", main_val)); gtk_box_append(GTK_BOX(hero_box), hero_lbl);
        GtkWidget *hero_sub = gtk_label_new("Download Size"); gtk_widget_add_css_class(hero_sub, "title-4"); gtk_box_append(GTK_BOX(hero_box), hero_sub);
        GtkWidget *row1 = adw_action_row_new(); adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row1), "App Package"); adw_action_row_set_subtitle(ADW_ACTION_ROW(row1), "The application itself"); GtkWidget *sz1 = gtk_label_new(main_val); gtk_widget_add_css_class(sz1, "dim-label"); adw_action_row_add_prefix(ADW_ACTION_ROW(row1), sz1); gtk_list_box_append(GTK_LIST_BOX(list), row1);
        GtkWidget *row2 = adw_action_row_new(); adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row2), "Required Dependencies"); adw_action_row_set_subtitle(ADW_ACTION_ROW(row2), "Shared components required by this app"); GtkWidget *sz2 = gtk_label_new("Variable"); gtk_widget_add_css_class(sz2, "dim-label"); adw_action_row_add_prefix(ADW_ACTION_ROW(row2), sz2); gtk_list_box_append(GTK_LIST_BOX(list), row2);
    } else if (g_strcmp0(tile_id, "SAFE") == 0) {
        gboolean is_safe = (strstr(main_val, "Safe") || strstr(main_val, "Sandboxed"));
        GtkWidget *hero_icon = gtk_image_new_from_icon_name(is_safe ? "security-high-symbolic" : "dialog-warning-symbolic"); gtk_image_set_pixel_size(GTK_IMAGE(hero_icon), 64); gtk_widget_add_css_class(hero_icon, is_safe ? "safe-color" : "warning-color"); gtk_box_append(GTK_BOX(hero_box), hero_icon);
        GtkWidget *hero_sub = gtk_label_new(main_val); gtk_widget_add_css_class(hero_sub, "title-4"); gtk_box_append(GTK_BOX(hero_box), hero_sub);
        if (!is_safe) {
            gtk_list_box_append(GTK_LIST_BOX(list), create_rich_row("network-wireless-symbolic", "Network Access", "Can access the internet", "icon-warn"));
            gtk_list_box_append(GTK_LIST_BOX(list), create_rich_row("folder-open-symbolic", "File System Access", "Can read and write to your system", "icon-warn"));
            gtk_list_box_append(GTK_LIST_BOX(list), create_rich_row("preferences-system-symbolic", "Session Services", "Can talk to non-portal services", "icon-warn"));
        } else {
            gtk_list_box_append(GTK_LIST_BOX(list), create_rich_row("camera-hardware-disabled-symbolic", "No Device Access", "Cannot access devices like webcams", "icon-safe"));
            gtk_list_box_append(GTK_LIST_BOX(list), create_rich_row("folder-symbolic", "Isolated Files", "Cannot access files outside the sandbox", "icon-safe"));
            gtk_list_box_append(GTK_LIST_BOX(list), create_rich_row("security-high-symbolic", "Strict Sandbox", "Runs safely isolated from host system", "icon-safe"));
        }
    } else if (g_strcmp0(tile_id, "AGE") == 0) {
        GtkWidget *hero_badge = gtk_label_new("All"); gtk_widget_add_css_class(hero_badge, "bento-badge"); gtk_widget_add_css_class(hero_badge, "badge-safe"); gtk_box_append(GTK_BOX(hero_box), hero_badge);
        GtkWidget *hero_sub = gtk_label_new("App is suitable for everyone"); gtk_widget_add_css_class(hero_sub, "title-4"); gtk_box_append(GTK_BOX(hero_box), hero_sub);
        gtk_list_box_append(GTK_LIST_BOX(list), create_rich_row("banknote-symbolic", "Money", "No ads or monetary transactions", "icon-safe"));
        gtk_list_box_append(GTK_LIST_BOX(list), create_rich_row("system-users-symbolic", "Social", "No uncontrolled chat functionality", "icon-safe"));
        gtk_list_box_append(GTK_LIST_BOX(list), create_rich_row("face-sick-symbolic", "Drugs", "No references to drugs", "icon-safe"));
        gtk_list_box_append(GTK_LIST_BOX(list), create_rich_row("avatar-default-symbolic", "Nudity", "No sex or nudity", "icon-safe"));
        gtk_list_box_append(GTK_LIST_BOX(list), create_rich_row("edit-undo-symbolic", "Strong Language", "No profanity", "icon-safe"));
        gtk_list_box_append(GTK_LIST_BOX(list), create_rich_row("dialog-error-symbolic", "Violence", "No violence", "icon-safe"));
    } else if (g_strcmp0(tile_id, "DESK") == 0) {
        GtkWidget *hero_icon = gtk_image_new_from_icon_name("computer-symbolic"); gtk_image_set_pixel_size(GTK_IMAGE(hero_icon), 64); gtk_box_append(GTK_BOX(hero_box), hero_icon);
        GtkWidget *hero_sub = gtk_label_new("App works on this device"); gtk_widget_add_css_class(hero_sub, "title-4"); gtk_box_append(GTK_BOX(hero_box), hero_sub);
        gtk_list_box_append(GTK_LIST_BOX(list), create_rich_row("computer-symbolic", "Desktop Support", "Fully optimized for large screens", "icon-safe"));
        gtk_list_box_append(GTK_LIST_BOX(list), create_rich_row("smartphone-symbolic", "Mobile Support Unknown", "Not optimized for small screens", "icon-warn"));
        gtk_list_box_append(GTK_LIST_BOX(list), create_rich_row("input-keyboard-symbolic", "Keyboard Support", "Keyboard input fully supported", "icon-safe"));
    } else if (g_strcmp0(tile_id, "VER") == 0) {
        GtkWidget *hero_lbl = gtk_label_new(NULL); gtk_label_set_markup(GTK_LABEL(hero_lbl), g_strdup_printf("<span font_size='xx-large' font_weight='bold'>%s</span>", main_val)); gtk_box_append(GTK_BOX(hero_box), hero_lbl);
        GtkWidget *hero_sub = gtk_label_new("Release Notes"); gtk_widget_add_css_class(hero_sub, "title-4"); gtk_box_append(GTK_BOX(hero_box), hero_sub);
        gtk_widget_set_visible(list, FALSE); 
        GtkWidget *log_lbl = gtk_label_new(sub_val); gtk_label_set_wrap(GTK_LABEL(log_lbl), TRUE); gtk_label_set_justify(GTK_LABEL(log_lbl), GTK_JUSTIFY_LEFT); gtk_widget_set_halign(log_lbl, GTK_ALIGN_START); gtk_box_append(GTK_BOX(content), log_lbl);
    } else if (g_strcmp0(tile_id, "ALT") == 0) {
        GtkWidget *hero_icon = gtk_image_new_from_icon_name("system-software-install-symbolic"); gtk_image_set_pixel_size(GTK_IMAGE(hero_icon), 64); gtk_box_append(GTK_BOX(hero_box), hero_icon);
        GtkWidget *hero_sub = gtk_label_new(main_val); gtk_widget_add_css_class(hero_sub, "title-4"); gtk_box_append(GTK_BOX(hero_box), hero_sub);
        gtk_widget_set_visible(list, FALSE); 
        GtkWidget *log_lbl = gtk_label_new(sub_val); gtk_label_set_wrap(GTK_LABEL(log_lbl), TRUE); gtk_label_set_justify(GTK_LABEL(log_lbl), GTK_JUSTIFY_CENTER); gtk_box_append(GTK_BOX(content), log_lbl);
    }

    GtkWidget *scroll = gtk_scrolled_window_new(); gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), content); adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(view), scroll);
    adw_dialog_set_child(ADW_DIALOG(dialog), view); adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(gtk_widget_get_root(GTK_WIDGET(btn))));
}

gboolean apply_media(gpointer data) {
    MediaTask *task = (MediaTask*)data;
    gchar *safe_id = g_strdup(task->app_id);
    for (int i = 0; safe_id[i]; i++) { if (safe_id[i] == '/') safe_id[i] = '_'; }
    char icon_path[1024], screen_path[1024], meta_path[1024];
    snprintf(icon_path, sizeof(icon_path), "/tmp/nixpkg/img/%s_icon.png", safe_id);
    snprintf(screen_path, sizeof(screen_path), "/tmp/nixpkg/img/%s_screen.png", safe_id);
    snprintf(meta_path, sizeof(meta_path), "/tmp/nixpkg/metadata/%s_meta.txt", safe_id);
    GError *err = NULL;

    if (task->icon_widget && access(icon_path, F_OK) == 0) {
        GdkTexture *tex = gdk_texture_new_from_filename(icon_path, &err);
        if (tex) { gtk_image_set_from_paintable(GTK_IMAGE(task->icon_widget), GDK_PAINTABLE(tex)); gtk_image_set_pixel_size(GTK_IMAGE(task->icon_widget), 32); g_object_unref(tex); } 
        else if (err) { g_error_free(err); err = NULL; }
    }

    const char *current_id = g_object_get_data(G_OBJECT(details_install_btn), "raw_id");
    if (current_id && strcmp(current_id, task->app_id) == 0) {
        if (access(icon_path, F_OK) == 0) {
            GdkTexture *tex = gdk_texture_new_from_filename(icon_path, &err);
            if (tex) { gtk_image_set_from_paintable(GTK_IMAGE(details_icon_img), GDK_PAINTABLE(tex)); gtk_image_set_pixel_size(GTK_IMAGE(details_icon_img), 96); g_object_unref(tex); } 
            else if (err) { g_error_free(err); err = NULL; }
        }
        if (access(screen_path, F_OK) == 0) {
            GdkTexture *tex = gdk_texture_new_from_filename(screen_path, &err);
            if (tex) { gtk_image_set_from_paintable(GTK_IMAGE(details_screenshot_img), GDK_PAINTABLE(tex)); gtk_image_set_pixel_size(GTK_IMAGE(details_screenshot_img), -1); gtk_widget_set_visible(details_screenshot_box, TRUE); g_object_unref(tex); } 
            else { gtk_widget_set_visible(details_screenshot_box, FALSE); if (err) { g_error_free(err); err = NULL; } }
        } else gtk_widget_set_visible(details_screenshot_box, FALSE);
        
        FILE *f = fopen(meta_path, "r");
        if (f) {
            char m_desc[1024]="", m_lic[128]="", m_home[256]="", m_ver[128]="", m_size[64]="", m_safestat[64]="", m_safedesc[128]="", m_age[32]="", m_change[2048]="", m_alt[64]="", m_dev[128]="", m_bug[256]="", m_help[256]="", m_trans[256]="", m_is_gui[32]="";
            if (fgets(m_desc, sizeof(m_desc), f)) m_desc[strcspn(m_desc, "\n")] = 0;
            if (fgets(m_lic, sizeof(m_lic), f)) m_lic[strcspn(m_lic, "\n")] = 0;
            if (fgets(m_home, sizeof(m_home), f)) m_home[strcspn(m_home, "\n")] = 0;
            if (fgets(m_ver, sizeof(m_ver), f)) m_ver[strcspn(m_ver, "\n")] = 0;
            if (fgets(m_size, sizeof(m_size), f)) m_size[strcspn(m_size, "\n")] = 0;
            if (fgets(m_safestat, sizeof(m_safestat), f)) m_safestat[strcspn(m_safestat, "\n")] = 0;
            if (fgets(m_safedesc, sizeof(m_safedesc), f)) m_safedesc[strcspn(m_safedesc, "\n")] = 0;
            if (fgets(m_age, sizeof(m_age), f)) m_age[strcspn(m_age, "\n")] = 0;
            if (fgets(m_change, sizeof(m_change), f)) m_change[strcspn(m_change, "\n")] = 0;
            if (fgets(m_alt, sizeof(m_alt), f)) m_alt[strcspn(m_alt, "\n")] = 0;
            if (fgets(m_dev, sizeof(m_dev), f)) m_dev[strcspn(m_dev, "\n")] = 0;
            if (fgets(m_bug, sizeof(m_bug), f)) m_bug[strcspn(m_bug, "\n")] = 0;
            if (fgets(m_help, sizeof(m_help), f)) m_help[strcspn(m_help, "\n")] = 0;
            if (fgets(m_trans, sizeof(m_trans), f)) m_trans[strcspn(m_trans, "\n")] = 0;
            if (fgets(m_is_gui, sizeof(m_is_gui), f)) m_is_gui[strcspn(m_is_gui, "\n")] = 0;
            fclose(f);
            
            if (strlen(m_desc) > 0 && strcmp(m_desc, "Unknown") != 0) gtk_label_set_text(GTK_LABEL(details_desc), m_desc);
            if (strlen(m_dev) > 0) gtk_label_set_text(GTK_LABEL(details_dev_lbl), m_dev);
            
            gtk_label_set_text(GTK_LABEL(bento_size_badge), strlen(m_size) > 0 ? m_size : "Variable");
            g_object_set_data_full(G_OBJECT(tile_1), "tile_id", g_strdup("SIZE"), g_free); g_object_set_data_full(G_OBJECT(tile_1), "dialog_main_val", g_strdup(m_size), g_free);

            gtk_label_set_text(GTK_LABEL(bento_safe_title), m_safestat); gtk_label_set_text(GTK_LABEL(bento_safe_sub), m_safedesc);
            g_object_set_data_full(G_OBJECT(tile_2), "tile_id", g_strdup("SAFE"), g_free); g_object_set_data_full(G_OBJECT(tile_2), "dialog_main_val", g_strdup(m_safestat), g_free);
            
            if (strstr(m_safestat, "Safe") || strstr(m_safestat, "Sandboxed")) {
                gtk_image_set_from_icon_name(GTK_IMAGE(bento_safe_icon), "security-high-symbolic"); gtk_widget_remove_css_class(bento_safe_icon, "warning-color"); gtk_widget_add_css_class(bento_safe_icon, "safe-color");
            } else {
                gtk_image_set_from_icon_name(GTK_IMAGE(bento_safe_icon), "dialog-warning-symbolic"); gtk_widget_remove_css_class(bento_safe_icon, "safe-color"); gtk_widget_add_css_class(bento_safe_icon, "warning-color");
            }

            g_object_set_data_full(G_OBJECT(tile_3), "tile_id", g_strdup("DESK"), g_free); g_object_set_data_full(G_OBJECT(tile_4), "tile_id", g_strdup("AGE"), g_free);

            char ver_str[256]; snprintf(ver_str, sizeof(ver_str), "Version %s", m_ver);
            gtk_label_set_text(GTK_LABEL(bento_ver_title), ver_str); gtk_label_set_text(GTK_LABEL(bento_change_lbl), strlen(m_change) > 0 ? m_change : "No release notes available.");
            g_object_set_data_full(G_OBJECT(version_card), "tile_id", g_strdup("VER"), g_free); g_object_set_data_full(G_OBJECT(version_card), "dialog_main_val", g_strdup(ver_str), g_free); g_object_set_data_full(G_OBJECT(version_card), "dialog_sub_val", g_strdup(m_change), g_free);

            gboolean is_unfree = (strstr(m_lic, "Unfree") || strstr(m_lic, "Proprietary") || strstr(m_lic, "Non-free"));
            if (is_unfree) {
                gtk_label_set_text(GTK_LABEL(comm_card_title), "Proprietary Software"); gtk_label_set_text(GTK_LABEL(comm_card_desc), "This app is proprietary. Its source code is closed, and it may not respect your privacy.");
                gtk_label_set_text(GTK_LABEL(comm_card_license), m_lic); gtk_image_set_from_icon_name(GTK_IMAGE(comm_card_icon1), "dialog-warning-symbolic"); gtk_image_set_from_icon_name(GTK_IMAGE(comm_card_icon2), "emblem-readonly-symbolic"); gtk_image_set_from_icon_name(GTK_IMAGE(comm_card_icon3), "dialog-error-symbolic");
                gtk_widget_add_css_class(comm_card_icon1, "comm-icon-warn"); gtk_widget_add_css_class(comm_card_icon2, "comm-icon-warn"); gtk_widget_add_css_class(comm_card_icon3, "comm-icon-warn");
            } else {
                gtk_label_set_text(GTK_LABEL(comm_card_title), "Community Built"); gtk_label_set_text(GTK_LABEL(comm_card_desc), "This app is developed in the open by an international community. You can participate and help make it even better.");
                gtk_label_set_text(GTK_LABEL(comm_card_license), m_lic); gtk_image_set_from_icon_name(GTK_IMAGE(comm_card_icon1), "emblem-favorite-symbolic"); gtk_image_set_from_icon_name(GTK_IMAGE(comm_card_icon2), "system-users-symbolic"); gtk_image_set_from_icon_name(GTK_IMAGE(comm_card_icon3), "emblem-ok-symbolic");
                gtk_widget_remove_css_class(comm_card_icon1, "comm-icon-warn"); gtk_widget_remove_css_class(comm_card_icon2, "comm-icon-warn"); gtk_widget_remove_css_class(comm_card_icon3, "comm-icon-warn");
            }

            if (strcmp(m_alt, "NIXOS") == 0 || strcmp(m_alt, "FLATPAK") == 0) {
                if (strcmp(m_alt, "NIXOS") == 0) {
                    gtk_label_set_text(GTK_LABEL(bento_alt_title), "NixOS Version Available"); gtk_label_set_text(GTK_LABEL(bento_alt_sub), "A native system package exists for this app in the Nixpkgs registry.");
                    gtk_widget_remove_css_class(tile_alt, "pkg-flatpak"); gtk_widget_add_css_class(tile_alt, "pkg-nix");
                } else {
                    gtk_label_set_text(GTK_LABEL(bento_alt_title), "Flathub Version Available"); gtk_label_set_text(GTK_LABEL(bento_alt_sub), "A sandboxed flatpak exists for this app on Flathub.");
                    gtk_widget_remove_css_class(tile_alt, "pkg-nix"); gtk_widget_add_css_class(tile_alt, "pkg-flatpak");
                }
                gtk_widget_set_visible(tile_alt, TRUE);
                g_object_set_data_full(G_OBJECT(tile_alt), "tile_id", g_strdup("ALT"), g_free);
                g_object_set_data_full(G_OBJECT(tile_alt), "dialog_main_val", g_strdup(strcmp(m_alt,"NIXOS")==0 ? "NixOS Alternative" : "Flathub Alternative"), g_free);
                g_object_set_data_full(G_OBJECT(tile_alt), "dialog_sub_val", g_strdup("NIXPAK detected that this software is available on multiple repositories. Use the main search page to explore the other version."), g_free);
            } else { gtk_widget_set_visible(tile_alt, FALSE); }

            adw_action_row_set_subtitle(ADW_ACTION_ROW(link_web_row), strlen(m_home) > 0 ? m_home : "Not available"); g_object_set_data_full(G_OBJECT(link_web_row), "url", g_strdup(m_home), g_free);
            adw_action_row_set_subtitle(ADW_ACTION_ROW(link_bug_row), strlen(m_bug) > 0 ? m_bug : "Not available"); g_object_set_data_full(G_OBJECT(link_bug_row), "url", g_strdup(m_bug), g_free);
            adw_action_row_set_subtitle(ADW_ACTION_ROW(link_help_row), strlen(m_help) > 0 ? m_help : "Not available"); g_object_set_data_full(G_OBJECT(link_help_row), "url", g_strdup(m_help), g_free);
            adw_action_row_set_subtitle(ADW_ACTION_ROW(link_trans_row), strlen(m_trans) > 0 ? m_trans : "Not available"); g_object_set_data_full(G_OBJECT(link_trans_row), "url", g_strdup(m_trans), g_free);
            
            g_object_set_data_full(G_OBJECT(details_try_btn), "is_gui", g_strdup(m_is_gui), g_free);

            if (tile_1) gtk_widget_set_visible(tile_1, cfg_show_size);
            if (tile_2) gtk_widget_set_visible(tile_2, cfg_show_safe);
            if (tile_3) gtk_widget_set_visible(tile_3, cfg_show_desk);
            if (tile_4) gtk_widget_set_visible(tile_4, cfg_show_age);
            gtk_button_set_label(GTK_BUTTON(details_store_lbl), (strcmp(task->type, "FLATPAK") == 0) ? "Flathub" : "Nixpkgs");
        }
    }
    g_free(safe_id); g_free(task->app_id); g_free(task->type); g_free(task);
    return G_SOURCE_REMOVE;
}

gpointer fetch_media_thread(gpointer data) {
    MediaTask *task = (MediaTask*)data;
    char cmd[4096]; gchar *quoted_id = g_shell_quote(task->app_id); gchar *quoted_type = g_shell_quote(task->type);
    snprintf(cmd, sizeof(cmd), "/usr/local/bin/nixpak-backend.sh details %s %s", quoted_id, quoted_type);
    int ret = system(cmd); (void)ret; 
    g_free(quoted_id); g_free(quoted_type);
    g_idle_add(apply_media, task); return NULL;
}

static void on_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
    AppResult *res = (AppResult*)g_object_get_data(G_OBJECT(row), "app_data");
    if (!res) return;

    gtk_label_set_markup(GTK_LABEL(details_title), g_strdup_printf("<span font_size='xx-large' font_weight='bold'>%s</span>", res->name));
    gtk_label_set_text(GTK_LABEL(details_dev_lbl), "Fetching..."); gtk_label_set_text(GTK_LABEL(details_desc), res->desc);
    gtk_image_set_from_icon_name(GTK_IMAGE(details_icon_img), get_fallback_icon(res->app_id)); gtk_image_set_pixel_size(GTK_IMAGE(details_icon_img), 96);
    gtk_widget_set_visible(details_screenshot_box, FALSE); 
    
    const char *store_name = (strcmp(res->type, "FLATPAK") == 0) ? "Flathub" : "Nixpkgs";
    adw_action_row_set_subtitle(ADW_ACTION_ROW(prov_row_attr), res->app_id);
    adw_action_row_set_subtitle(ADW_ACTION_ROW(prov_row_arch), "x86_64-linux");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(prov_row_store), store_name);
    adw_action_row_set_subtitle(ADW_ACTION_ROW(prov_row_license), "Fetching...");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(prov_row_home), "Fetching...");

    gtk_widget_set_visible(details_try_btn, TRUE);
    g_object_set_data_full(G_OBJECT(details_try_btn), "app_id", g_strdup(res->app_id), g_free);
    g_object_set_data_full(G_OBJECT(details_try_btn), "type", g_strdup(res->type), g_free);

    if (strcmp(res->type, "FLATPAK") == 0) {
        gtk_widget_set_visible(details_nix_code, FALSE); gtk_button_set_icon_name(GTK_BUTTON(details_store_lbl), "package-x-generic-symbolic");
    } else {
        gtk_label_set_markup(GTK_LABEL(details_nix_code), g_strdup_printf("environment.systemPackages = with pkgs; [ \n  <b>%s</b> \n];", res->app_id));
        gtk_widget_set_visible(details_nix_code, TRUE); gtk_button_set_icon_name(GTK_BUTTON(details_store_lbl), "system-software-install-symbolic");
    }

    gboolean installed = check_installed(res->app_id, res->type);
    gtk_button_set_label(GTK_BUTTON(details_install_btn), installed ? "Remove" : "Install");
    if (installed) { gtk_widget_remove_css_class(details_install_btn, "suggested-action"); gtk_widget_add_css_class(details_install_btn, "destructive-action"); } 
    else { gtk_widget_remove_css_class(details_install_btn, "destructive-action"); gtk_widget_add_css_class(details_install_btn, "suggested-action"); }

    g_object_set_data_full(G_OBJECT(details_install_btn), "raw_id", g_strdup(res->app_id), g_free);
    g_object_set_data_full(G_OBJECT(details_install_btn), "type", g_strdup(res->type), g_free);
    gchar *install_id = (strcmp(res->type, "FLATPAK") == 0) ? g_strdup_printf("flatpak:%s", res->app_id) : g_strdup(res->app_id);
    g_object_set_data_full(G_OBJECT(details_install_btn), "app_id", install_id, g_free);
    g_object_set_data(G_OBJECT(details_install_btn), "action", GINT_TO_POINTER(installed ? 1 : 0));

    MediaTask *mtask = g_new(MediaTask, 1); mtask->app_id = g_strdup(res->app_id); mtask->type = g_strdup(res->type); mtask->icon_widget = NULL; 
    g_thread_new("fetch_details_media", fetch_media_thread, mtask);
    gtk_stack_set_visible_child_name(GTK_STACK(main_stack), "details");
}

static void trigger_try_shell(GtkWidget *btn_widget) {
    const char *app_id = g_object_get_data(G_OBJECT(btn_widget), "app_id");
    const char *type = g_object_get_data(G_OBJECT(btn_widget), "type");
    const char *is_gui = g_object_get_data(G_OBJECT(btn_widget), "is_gui");
    
    int sys_ret = system("mkdir -p /tmp/nixpkg/scripts"); (void)sys_ret;
    char script_path[1024];
    snprintf(script_path, sizeof(script_path), "/tmp/nixpkg/scripts/launch_%s.sh", app_id);
    
    FILE *f = fopen(script_path, "w");
    if(f) {
        fprintf(f, "#!/bin/sh\n");
        fprintf(f, "PATH=$PATH:/run/current-system/sw/bin:/usr/bin:/bin\n");
        if (g_strcmp0(is_gui, "True") == 0) {
            if (g_strcmp0(type, "FLATPAK") == 0) {
                fprintf(f, "if command -v cage >/dev/null 2>&1; then cage -- flatpak run %s; else flatpak run %s; fi\n", app_id, app_id);
            } else {
                fprintf(f, "if command -v cage >/dev/null 2>&1; then\n");
                fprintf(f, "  if command -v bwrap >/dev/null 2>&1; then\n");
                fprintf(f, "    CMD=\"bwrap --dev-bind /dev /dev --ro-bind /nix /nix --ro-bind /etc /etc --bind /run /run --proc /proc --tmpfs /tmp --tmpfs /home --share-net nix-shell -p %s --run %s\"\n", app_id, app_id);
                fprintf(f, "  else\n");
                fprintf(f, "    CMD=\"nix-shell -p %s --run %s\"\n", app_id, app_id);
                fprintf(f, "  fi\n");
                fprintf(f, "  cage -- sh -c \"if command -v kgx >/dev/null 2>&1; then kgx -e sh -c \\\"echo Loading NixOS Environment... && \\$CMD\\\"; elif command -v konsole >/dev/null 2>&1; then konsole -e sh -c \\\"echo Loading NixOS Environment... && \\$CMD\\\"; else xterm -e sh -c \\\"echo Loading NixOS Environment... && \\$CMD\\\"; fi\"\n");
                fprintf(f, "else\n");
                fprintf(f, "  nix-shell -p %s --run %s\n", app_id, app_id);
                fprintf(f, "fi\n");
            }
        } else {
            if (g_strcmp0(type, "FLATPAK") == 0) {
                fprintf(f, "if command -v kgx >/dev/null 2>&1; then kgx -e 'flatpak run --command=sh %s'; elif command -v gnome-terminal >/dev/null 2>&1; then gnome-terminal -- sh -c 'flatpak run --command=sh %s'; elif command -v konsole >/dev/null 2>&1; then konsole -e 'flatpak run --command=sh %s'; else xterm -e 'flatpak run --command=sh %s'; fi\n", app_id, app_id, app_id, app_id);
            } else {
                fprintf(f, "if command -v kgx >/dev/null 2>&1; then kgx -e 'nix-shell -p %s'; elif command -v gnome-terminal >/dev/null 2>&1; then gnome-terminal -- sh -c 'nix-shell -p %s'; elif command -v konsole >/dev/null 2>&1; then konsole -e 'nix-shell -p %s'; else xterm -e 'nix-shell -p %s'; fi\n", app_id, app_id, app_id, app_id);
            }
        }
        fclose(f);
        chmod(script_path, 0755);
    }
    
    char shell_wrapper[2048];
    snprintf(shell_wrapper, sizeof(shell_wrapper), "sh -c '%s'", script_path);
    g_spawn_command_line_async(shell_wrapper, NULL);
}

void add_result_card(AppResult *res) {
    GtkWidget *row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), res->name);
    adw_action_row_set_subtitle(ADW_ACTION_ROW(row), res->desc);
    gtk_widget_add_css_class(row, (strcmp(res->type, "NIX") == 0) ? "pkg-nix" : "pkg-flatpak");

    GtkWidget *icon = gtk_image_new_from_icon_name(get_fallback_icon(res->app_id));
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 32); adw_action_row_add_prefix(ADW_ACTION_ROW(row), icon);

    MediaTask *mtask = g_new(MediaTask, 1); mtask->app_id = g_strdup(res->app_id); mtask->type = g_strdup(res->type); mtask->icon_widget = icon;
    g_thread_new("fetch_list_media", fetch_media_thread, mtask);

    gboolean installed = check_installed(res->app_id, res->type);
    GtkWidget *btn = gtk_button_new_with_label(installed ? "Remove" : "Install");
    gtk_widget_set_valign(btn, GTK_ALIGN_CENTER); gtk_widget_add_css_class(btn, installed ? "destructive-action" : "suggested-action"); gtk_widget_add_css_class(btn, "pill");

    g_object_set_data_full(G_OBJECT(btn), "raw_id", g_strdup(res->app_id), g_free); g_object_set_data_full(G_OBJECT(btn), "type", g_strdup(res->type), g_free);
    gchar *install_id = (strcmp(res->type, "FLATPAK") == 0) ? g_strdup_printf("flatpak:%s", res->app_id) : g_strdup(res->app_id);
    g_object_set_data_full(G_OBJECT(btn), "app_id", install_id, g_free); g_object_set_data(G_OBJECT(btn), "action", GINT_TO_POINTER(installed ? 1 : 0));
    g_signal_connect_swapped(btn, "clicked", G_CALLBACK(trigger_action), btn);

    adw_action_row_add_suffix(ADW_ACTION_ROW(row), btn); gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), TRUE);
    
    AppResult *saved_res = g_new0(AppResult, 1); saved_res->type = g_strdup(res->type); saved_res->name = g_strdup(res->name); saved_res->desc = g_strdup(res->desc); saved_res->app_id = g_strdup(res->app_id);
    g_object_set_data_full(G_OBJECT(row), "app_data", saved_res, free_app_result); gtk_list_box_append(GTK_LIST_BOX(results_list), row);
}

gboolean add_generation_row(gpointer data) {
    char *gen_str = (char*)data; if (strlen(gen_str) == 0) { g_free(gen_str); return G_SOURCE_REMOVE; }
    GtkWidget *row = adw_action_row_new(); adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), gen_str);
    GtkWidget *icon = gtk_image_new_from_icon_name("system-software-install-symbolic"); adw_action_row_add_prefix(ADW_ACTION_ROW(row), icon);
    gtk_list_box_append(GTK_LIST_BOX(gens_list), row); g_free(gen_str); return G_SOURCE_REMOVE;
}

gboolean pulse_progress(gpointer d) { gtk_progress_bar_pulse(GTK_PROGRESS_BAR(progress_bar)); return G_SOURCE_REMOVE; }

gboolean append_log(gpointer data) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_view));
    GtkTextIter end; gtk_text_buffer_get_end_iter(buffer, &end); gtk_text_buffer_insert(buffer, &end, (char *)data, -1);
    gint line_count = gtk_text_buffer_get_line_count(buffer);
    if (line_count > 500) { GtkTextIter start, del_end; gtk_text_buffer_get_start_iter(buffer, &start); gtk_text_buffer_get_iter_at_line(buffer, &del_end, line_count - 500); gtk_text_buffer_delete(buffer, &start, &del_end); }
    gtk_text_buffer_get_end_iter(buffer, &end); GtkTextMark *mark = gtk_text_buffer_create_mark(buffer, NULL, &end, FALSE);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(log_view), mark); gtk_text_buffer_delete_mark(buffer, mark);
    g_idle_add(pulse_progress, NULL); g_free(data); return G_SOURCE_REMOVE;
}

gboolean finish_task(gpointer d) { 
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), 1.0); TaskData *task = (TaskData *)d;
    if (task && (task->action == 0 || task->action == 1)) {
        const char *query = gtk_editable_get_text(GTK_EDITABLE(search_entry));
        if (strlen(query) > 0 && query[0] != ':') {
            GtkWidget *child; while ((child = gtk_widget_get_first_child(results_list)) != NULL) { gtk_list_box_remove(GTK_LIST_BOX(results_list), child); }
            TaskData *refresh_task = g_malloc0(sizeof(TaskData)); refresh_task->query = g_strdup(query); refresh_task->action = 2; g_thread_new("search_refresh", background_task, refresh_task);
        }
    }
    if (task && (task->action == 0 || task->action == 1 || task->action >= 3)) g_idle_add(update_stats_ui, NULL);
    if (task) { if (task->query) g_free(task->query); g_free(task); }
    return G_SOURCE_REMOVE; 
}

gpointer background_task(gpointer data) {
    TaskData *task = (TaskData *)data; char cmd[4096];
    if (task->action == 0 || task->action == 1) { 
        gchar *quoted_app = g_shell_quote(task->query); snprintf(cmd, sizeof(cmd), "PATH=$PATH:/run/current-system/sw/bin:/usr/bin /usr/local/bin/nixpkg %s %s 2>&1", task->action == 0 ? "install" : "remove", quoted_app); g_free(quoted_app);
        FILE *fp = popen(cmd, "r"); if (fp) { char buffer[512]; while (fgets(buffer, sizeof(buffer), fp) != NULL) g_idle_add(append_log, g_strdup(buffer)); pclose(fp); }
        g_idle_add(append_log, g_strdup("\n>>> Executing system rebuild...\n"));
        FILE *fp2 = popen("PATH=$PATH:/run/current-system/sw/bin:/usr/bin /usr/local/bin/nixpkg test 2>&1", "r");
        if (fp2) { char buffer[512]; while (fgets(buffer, sizeof(buffer), fp2) != NULL) g_idle_add(append_log, g_strdup(buffer)); pclose(fp2); }
    } else if (task->action == 3) { FILE *fp = popen("PATH=$PATH:/run/current-system/sw/bin:/usr/bin /usr/local/bin/nixpkg gc 2>&1", "r"); if (fp) { char buffer[512]; while (fgets(buffer, sizeof(buffer), fp) != NULL) g_idle_add(append_log, g_strdup(buffer)); pclose(fp); }
    } else if (task->action == 4) { FILE *fp = popen("PATH=$PATH:/run/current-system/sw/bin:/usr/bin /usr/local/bin/nixpkg test 2>&1", "r"); if (fp) { char buffer[512]; while (fgets(buffer, sizeof(buffer), fp) != NULL) g_idle_add(append_log, g_strdup(buffer)); pclose(fp); }
    } else if (task->action == 5) { FILE *fp = popen("PATH=$PATH:/run/current-system/sw/bin:/usr/bin /usr/local/bin/nixpkg switch 2>&1", "r"); if (fp) { char buffer[512]; while (fgets(buffer, sizeof(buffer), fp) != NULL) g_idle_add(append_log, g_strdup(buffer)); pclose(fp); }
    } else if (task->action == 6) { FILE *fp = popen("PATH=$PATH:/run/current-system/sw/bin:/usr/bin /usr/local/bin/nixpkg boot 2>&1", "r"); if (fp) { char buffer[512]; while (fgets(buffer, sizeof(buffer), fp) != NULL) g_idle_add(append_log, g_strdup(buffer)); pclose(fp); }
    } else if (task->action == 7) { FILE *fp = popen("PATH=$PATH:/run/current-system/sw/bin:/usr/bin /usr/local/bin/nixpkg rollback 2>&1", "r"); if (fp) { char buffer[512]; while (fgets(buffer, sizeof(buffer), fp) != NULL) g_idle_add(append_log, g_strdup(buffer)); pclose(fp); }
    } else if (task->action == 8) { FILE *fp = popen("PATH=$PATH:/run/current-system/sw/bin:/usr/bin ls -l /nix/var/nix/profiles/ | grep system- | awk '{print $9}' | sed 's/-link//' | sort -rV | head -n 15", "r"); if (fp) { char buffer[256]; while (fgets(buffer, sizeof(buffer), fp) != NULL) { buffer[strcspn(buffer, "\n")] = 0; g_idle_add(add_generation_row, g_strdup(buffer)); } pclose(fp); }
    } else if (task->action == 2) { 
        gchar *quoted_query = g_shell_quote(task->query); snprintf(cmd, sizeof(cmd), "/usr/local/bin/nixpak-backend.sh search %s", quoted_query); g_free(quoted_query);
        FILE *fp = popen(cmd, "r");
        if (fp) {
            char buffer[2048]; gboolean first_batch_scheduled = FALSE;
            while (fgets(buffer, sizeof(buffer), fp) != NULL) {
                buffer[strcspn(buffer, "\n")] = 0; gchar **parts = g_strsplit(buffer, "|", 5);
                if (parts[0] && parts[1] && parts[2] && parts[3]) {
                    AppResult *res = g_new0(AppResult, 1); res->type = g_strdup(parts[0]); res->name = g_strdup(parts[1]); res->desc = g_strdup(parts[2]); res->app_id = g_strdup(parts[3]); res->icon_url = parts[4] ? g_strdup(parts[4]) : NULL;
                    g_queue_push_tail(pending_results_queue, res);
                    if (!first_batch_scheduled && g_queue_get_length(pending_results_queue) >= 12) { g_idle_add(idle_initial_batch, NULL); first_batch_scheduled = TRUE; }
                } g_strfreev(parts);
            } pclose(fp); if (!first_batch_scheduled) g_idle_add(idle_initial_batch, NULL);
        }
    }
    g_idle_add(finish_task, task); return NULL;
}

static void on_gc_response(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    const char *response = adw_alert_dialog_choose_finish(ADW_ALERT_DIALOG(source_object), res);
    if (g_strcmp0(response, "accept") == 0) {
        gtk_stack_set_visible_child_name(GTK_STACK(main_stack), "progress"); gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_view)), "", -1); gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), 0.0); g_idle_add(append_log, g_strdup("Initiating System Garbage Collection...\n"));
        TaskData *task = g_malloc0(sizeof(TaskData)); task->query = g_strdup("GC"); task->action = 3; g_thread_new("gc_task", background_task, task);
    }
}

static void trigger_gc_confirm(GtkWidget *btn_widget) {
    AdwDialog *dialog = adw_alert_dialog_new("Run Garbage Collection?", "This will delete old system generations and unreferenced packages from the Nix Store. You will not be able to rollback to previous states.");
    adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dialog), "cancel", "Cancel"); adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dialog), "accept", "Clean System"); adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog), "accept", ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_choose(ADW_ALERT_DIALOG(dialog), GTK_WIDGET(gtk_widget_get_root(btn_widget)), NULL, on_gc_response, NULL);
}

static void trigger_system_action(GtkWidget *btn_widget) {
    int action = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn_widget), "action"));
    gtk_stack_set_visible_child_name(GTK_STACK(main_stack), "progress"); gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_view)), "", -1); gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), 0.0); g_idle_add(append_log, g_strdup("Executing system operation...\n"));
    TaskData *task = g_malloc0(sizeof(TaskData)); task->query = g_strdup("SYS"); task->action = action; g_thread_new("sys_task", background_task, task);
}

static void trigger_action(GtkWidget *btn_widget) {
    const char *app_id = g_object_get_data(G_OBJECT(btn_widget), "app_id");
    int action = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn_widget), "action"));
    gtk_stack_set_visible_child_name(GTK_STACK(main_stack), "progress"); gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_view)), "", -1); gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), 0.0); g_idle_add(append_log, g_strdup_printf("Starting %s for: %s\n", action == 0 ? "installation" : "removal", app_id));
    TaskData *task = g_malloc0(sizeof(TaskData)); task->query = g_strdup(app_id); task->action = action; g_thread_new("action_task", background_task, task);
}

static void on_search_changed(GtkSearchEntry *entry, gpointer d) {
    const char *query = gtk_editable_get_text(GTK_EDITABLE(entry));
    if (strlen(query) == 0) {
        gtk_stack_set_visible_child_name(GTK_STACK(main_stack), "view_stack"); gtk_widget_set_visible(btn_back, FALSE);
        GtkWidget *child; while ((child = gtk_widget_get_first_child(results_list)) != NULL) { gtk_list_box_remove(GTK_LIST_BOX(results_list), child); }
        if (pending_results_queue) { g_queue_free_full(pending_results_queue, free_app_result); pending_results_queue = g_queue_new(); }
    }
}

static void on_search_activated(GtkWidget *entry, gpointer d) {
    const char *query = gtk_editable_get_text(GTK_EDITABLE(entry));
    if (strlen(query) == 0) return;
    if (query[0] == ':') {
        if (strcmp(query, ":back") == 0) on_back_clicked(NULL, NULL);
        else if (strcmp(query, ":explore") == 0) { gtk_stack_set_visible_child_name(GTK_STACK(main_stack), "view_stack"); adw_view_stack_set_visible_child_name(ADW_VIEW_STACK(view_stack), "explore"); gtk_widget_set_visible(btn_back, FALSE); }
        else if (strcmp(query, ":system") == 0) { gtk_stack_set_visible_child_name(GTK_STACK(main_stack), "view_stack"); adw_view_stack_set_visible_child_name(ADW_VIEW_STACK(view_stack), "system"); gtk_widget_set_visible(btn_back, FALSE); }
        else if (strcmp(query, ":settings") == 0) on_settings_clicked(NULL, NULL);
        gtk_editable_set_text(GTK_EDITABLE(entry), ""); return;
    }
    GtkWidget *child; while ((child = gtk_widget_get_first_child(results_list)) != NULL) { gtk_list_box_remove(GTK_LIST_BOX(results_list), child); }
    if (pending_results_queue) { g_queue_free_full(pending_results_queue, free_app_result); } pending_results_queue = g_queue_new();
    gtk_widget_set_visible(btn_back, TRUE); gtk_stack_set_visible_child_name(GTK_STACK(main_stack), "search");
    TaskData *task = g_malloc0(sizeof(TaskData)); task->query = g_strdup(query); task->action = 2; g_thread_new("search_task", background_task, task);
}

static void on_card_clicked(GtkWidget *w, gpointer data) { gtk_editable_set_text(GTK_EDITABLE(search_entry), (const char *)data); g_signal_emit_by_name(search_entry, "activate"); }

GtkWidget* create_color_btn(const char *icon_name, const char *label, const char *css_name, const char *info_text) {
    GtkWidget *btn = gtk_button_new(); gtk_widget_add_css_class(btn, "category-btn"); gtk_widget_add_css_class(btn, css_name);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10); gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
    GtkWidget *icon = gtk_image_new_from_icon_name(icon_name); gtk_image_set_pixel_size(GTK_IMAGE(icon), 24);
    gtk_box_append(GTK_BOX(box), icon); gtk_box_append(GTK_BOX(box), gtk_label_new(label));
    gtk_button_set_child(GTK_BUTTON(btn), box); g_signal_connect(btn, "clicked", G_CALLBACK(on_card_clicked), (gpointer)info_text); return btn;
}

GtkWidget* create_action_btn(const char *label, int action_code) {
    GtkWidget *btn = gtk_button_new_with_label(label); gtk_widget_set_valign(btn, GTK_ALIGN_CENTER);
    g_object_set_data(G_OBJECT(btn), "action", GINT_TO_POINTER(action_code)); g_signal_connect_swapped(btn, "clicked", G_CALLBACK(trigger_system_action), btn); return btn;
}

GtkWidget* create_bento_button(GtkWidget *top_widget, GtkWidget **title_lbl, GtkWidget **sub_lbl) {
    GtkWidget *btn = gtk_button_new(); gtk_widget_add_css_class(btn, "bento-tile"); gtk_widget_add_css_class(btn, "flat"); gtk_widget_set_halign(btn, GTK_ALIGN_FILL); gtk_widget_set_valign(btn, GTK_ALIGN_FILL); gtk_widget_set_hexpand(btn, TRUE); gtk_widget_set_vexpand(btn, TRUE);
    g_signal_connect(btn, "clicked", G_CALLBACK(bento_tile_clicked), NULL);
    GtkWidget *inner = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6); gtk_widget_set_valign(inner, GTK_ALIGN_CENTER); gtk_widget_set_vexpand(inner, TRUE);
    gtk_widget_set_halign(top_widget, GTK_ALIGN_CENTER); gtk_box_append(GTK_BOX(inner), top_widget);
    *title_lbl = gtk_label_new(""); gtk_widget_add_css_class(*title_lbl, "title-4"); gtk_widget_set_halign(*title_lbl, GTK_ALIGN_CENTER); gtk_box_append(GTK_BOX(inner), *title_lbl);
    *sub_lbl = gtk_label_new(""); gtk_widget_add_css_class(*sub_lbl, "dim-label"); gtk_label_set_wrap(GTK_LABEL(*sub_lbl), TRUE); gtk_label_set_justify(GTK_LABEL(*sub_lbl), GTK_JUSTIFY_CENTER); gtk_widget_set_halign(*sub_lbl, GTK_ALIGN_CENTER); gtk_box_append(GTK_BOX(inner), *sub_lbl);
    gtk_button_set_child(GTK_BUTTON(btn), inner); return btn;
}

GtkWidget* create_horizontal_bento_button(GtkWidget *left_widget, GtkWidget **title_lbl, GtkWidget **sub_lbl) {
    GtkWidget *btn = gtk_button_new(); gtk_widget_add_css_class(btn, "bento-tile"); gtk_widget_add_css_class(btn, "flat"); gtk_widget_set_halign(btn, GTK_ALIGN_FILL); gtk_widget_set_valign(btn, GTK_ALIGN_FILL); gtk_widget_set_hexpand(btn, TRUE); gtk_widget_set_vexpand(btn, TRUE);
    g_signal_connect(btn, "clicked", G_CALLBACK(bento_tile_clicked), NULL);
    GtkWidget *inner = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16); gtk_widget_set_valign(inner, GTK_ALIGN_CENTER); gtk_widget_set_hexpand(inner, TRUE);
    gtk_widget_set_valign(left_widget, GTK_ALIGN_CENTER); gtk_box_append(GTK_BOX(inner), left_widget);
    GtkWidget *text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4); gtk_widget_set_valign(text_box, GTK_ALIGN_CENTER); gtk_widget_set_hexpand(text_box, TRUE);
    *title_lbl = gtk_label_new(""); gtk_widget_add_css_class(*title_lbl, "title-4"); gtk_widget_set_halign(*title_lbl, GTK_ALIGN_START); gtk_box_append(GTK_BOX(text_box), *title_lbl);
    *sub_lbl = gtk_label_new(""); gtk_widget_add_css_class(*sub_lbl, "dim-label"); gtk_label_set_wrap(GTK_LABEL(*sub_lbl), TRUE); gtk_label_set_justify(GTK_LABEL(*sub_lbl), GTK_JUSTIFY_LEFT); gtk_widget_set_halign(*sub_lbl, GTK_ALIGN_START); gtk_box_append(GTK_BOX(text_box), *sub_lbl);
    gtk_box_append(GTK_BOX(inner), text_box); gtk_button_set_child(GTK_BUTTON(btn), inner); return btn;
}

static void activate(GtkApplication *app, gpointer user_data) {
    load_config(); pending_results_queue = g_queue_new();

    GtkWidget *window = adw_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "NIXPAK");
    gtk_window_set_default_size(GTK_WINDOW(window), 1000, 800);

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider, css_style);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(window), main_box);

    GtkWidget *header = adw_header_bar_new();
    btn_back = gtk_button_new_from_icon_name("go-previous-symbolic");
    gtk_widget_set_visible(btn_back, FALSE);
    g_signal_connect(btn_back, "clicked", G_CALLBACK(on_back_clicked), NULL);
    adw_header_bar_pack_start(ADW_HEADER_BAR(header), btn_back);

    GtkWidget *header_ascii_lbl = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(header_ascii_lbl), header_ascii_logo);
    gtk_widget_set_margin_start(header_ascii_lbl, 10);
    gtk_widget_set_margin_top(header_ascii_lbl, 5);
    gtk_widget_set_margin_bottom(header_ascii_lbl, 5);
    gtk_widget_set_valign(header_ascii_lbl, GTK_ALIGN_CENTER);
    adw_header_bar_pack_start(ADW_HEADER_BAR(header), header_ascii_lbl);

    view_stack = adw_view_stack_new();
    GtkWidget *switcher = adw_view_switcher_new();
    adw_view_switcher_set_stack(ADW_VIEW_SWITCHER(switcher), ADW_VIEW_STACK(view_stack));
    adw_view_switcher_set_policy(ADW_VIEW_SWITCHER(switcher), ADW_VIEW_SWITCHER_POLICY_WIDE);
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), switcher);

    search_entry = gtk_search_entry_new();
    gtk_widget_set_size_request(search_entry, 300, -1);
    gtk_widget_set_valign(search_entry, GTK_ALIGN_CENTER);
    g_signal_connect(search_entry, "activate", G_CALLBACK(on_search_activated), NULL);
    g_signal_connect(search_entry, "search-changed", G_CALLBACK(on_search_changed), NULL);
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), search_entry);

    GtkWidget *btn_settings = gtk_button_new_from_icon_name("emblem-system-symbolic");
    gtk_widget_add_css_class(btn_settings, "flat");
    g_signal_connect(btn_settings, "clicked", G_CALLBACK(on_settings_clicked), NULL);
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), btn_settings);

    gtk_box_append(GTK_BOX(main_box), header);

    main_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(main_stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_widget_set_vexpand(main_stack, TRUE);
    gtk_box_append(GTK_BOX(main_box), main_stack);
    
    gtk_stack_add_named(GTK_STACK(main_stack), view_stack, "view_stack");

    results_list = gtk_list_box_new();
    gtk_widget_add_css_class(results_list, "boxed-list"); 
    gtk_widget_set_vexpand(results_list, TRUE);
    g_signal_connect(results_list, "row-activated", G_CALLBACK(on_row_activated), NULL);

    page_settings = gtk_scrolled_window_new();
    GtkWidget *set_margin = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_widget_set_margin_start(set_margin, 60); gtk_widget_set_margin_end(set_margin, 60); gtk_widget_set_margin_top(set_margin, 30);

    GtkWidget *set_group1 = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(set_group1), "Search Behavior");
    GtkWidget *row_lit = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row_lit), "Literal Search Matching");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(row_lit), "Filter out fuzzy results to enforce strict text matches.");
    sw_literal = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(sw_literal), strcmp(cfg_search_mode, "literal") == 0);
    gtk_widget_set_valign(sw_literal, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(ADW_ACTION_ROW(row_lit), sw_literal);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(set_group1), row_lit);
    gtk_box_append(GTK_BOX(set_margin), set_group1);

    GtkWidget *set_group2 = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(set_group2), "Bento Box Visibility");
    
    GtkWidget *row_sz = adw_action_row_new(); adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row_sz), "Download Size Tile");
    sw_size = gtk_switch_new(); gtk_switch_set_active(GTK_SWITCH(sw_size), cfg_show_size);
    gtk_widget_set_valign(sw_size, GTK_ALIGN_CENTER); adw_action_row_add_suffix(ADW_ACTION_ROW(row_sz), sw_size);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(set_group2), row_sz);

    GtkWidget *row_sf = adw_action_row_new(); adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row_sf), "Safety & Sandboxing Tile");
    sw_safe = gtk_switch_new(); gtk_switch_set_active(GTK_SWITCH(sw_safe), cfg_show_safe);
    gtk_widget_set_valign(sw_safe, GTK_ALIGN_CENTER); adw_action_row_add_suffix(ADW_ACTION_ROW(row_sf), sw_safe);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(set_group2), row_sf);

    GtkWidget *row_ds = adw_action_row_new(); adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row_ds), "Hardware Compatibility Tile");
    sw_desk = gtk_switch_new(); gtk_switch_set_active(GTK_SWITCH(sw_desk), cfg_show_desk);
    gtk_widget_set_valign(sw_desk, GTK_ALIGN_CENTER); adw_action_row_add_suffix(ADW_ACTION_ROW(row_ds), sw_desk);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(set_group2), row_ds);

    GtkWidget *row_ag = adw_action_row_new(); adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row_ag), "Age Rating Tile");
    sw_age = gtk_switch_new(); gtk_switch_set_active(GTK_SWITCH(sw_age), cfg_show_age);
    gtk_widget_set_valign(sw_age, GTK_ALIGN_CENTER); adw_action_row_add_suffix(ADW_ACTION_ROW(row_ag), sw_age);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(set_group2), row_ag);

    gtk_box_append(GTK_BOX(set_margin), set_group2);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(page_settings), set_margin);
    gtk_stack_add_named(GTK_STACK(main_stack), page_settings, "settings");

    g_signal_connect(sw_literal, "notify::active", G_CALLBACK(on_setting_changed), NULL);
    g_signal_connect(sw_size, "notify::active", G_CALLBACK(on_setting_changed), NULL);
    g_signal_connect(sw_safe, "notify::active", G_CALLBACK(on_setting_changed), NULL);
    g_signal_connect(sw_desk, "notify::active", G_CALLBACK(on_setting_changed), NULL);
    g_signal_connect(sw_age, "notify::active", G_CALLBACK(on_setting_changed), NULL);

    page_explore = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_widget_set_margin_start(page_explore, 40); gtk_widget_set_margin_end(page_explore, 40); gtk_widget_set_margin_top(page_explore, 20);
    
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 15); gtk_grid_set_column_spacing(GTK_GRID(grid), 15); 
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
    
    gtk_grid_attach(GTK_GRID(grid), create_color_btn("applications-graphics-symbolic", "Create", "cat-create", "blender"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), create_color_btn("applications-office-symbolic", "Work", "cat-work", "libreoffice"), 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), create_color_btn("applications-games-symbolic", "Play", "cat-play", "steam"), 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), create_color_btn("system-users-symbolic", "Socialize", "cat-social", "vesktop"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), create_color_btn("applications-science-symbolic", "Learn", "cat-learn", "anki"), 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), create_color_btn("applications-development-symbolic", "Develop", "cat-develop", "vscode"), 2, 1, 1, 1);
    gtk_box_append(GTK_BOX(page_explore), grid);

    GtkWidget *dashboard_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_vexpand(dashboard_box, TRUE);
    gtk_widget_set_valign(dashboard_box, GTK_ALIGN_CENTER);

    GtkWidget *logo_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(logo_label), "<span font_size='36pt' font_weight='bold'>NIXPAK</span>");
    gtk_widget_set_halign(logo_label, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(dashboard_box), logo_label);
    
    GtkWidget *legend_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(legend_label), "<span foreground='#5fb8f2'>●</span> NixOS Packages   <span foreground='#385970'>●</span> Flathub Flatpaks");
    gtk_widget_add_css_class(legend_label, "dim-label");
    gtk_widget_set_halign(legend_label, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(dashboard_box), legend_label);

    GtkWidget *sys_strip = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 15);
    gtk_widget_set_halign(sys_strip, GTK_ALIGN_CENTER);
    sys_stats_label = gtk_label_new(NULL);
    gtk_widget_add_css_class(sys_stats_label, "dim-label");
    gtk_box_append(GTK_BOX(sys_strip), sys_stats_label);
    
    g_idle_add(update_stats_ui, NULL);

    GtkWidget *btn_gc = gtk_button_new_with_label("Garbage Collect");
    gtk_widget_add_css_class(btn_gc, "pill");
    g_signal_connect_swapped(btn_gc, "clicked", G_CALLBACK(trigger_gc_confirm), btn_gc);
    gtk_box_append(GTK_BOX(sys_strip), btn_gc);

    gtk_box_append(GTK_BOX(dashboard_box), sys_strip);
    gtk_box_append(GTK_BOX(page_explore), dashboard_box);
    
    adw_view_stack_add_titled_with_icon(ADW_VIEW_STACK(view_stack), page_explore, "explore", "Explore", "edit-find-symbolic");

    page_system = gtk_scrolled_window_new();
    GtkWidget *system_margin = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_widget_set_margin_start(system_margin, 60); gtk_widget_set_margin_end(system_margin, 60); gtk_widget_set_margin_top(system_margin, 30);
    
    GtkWidget *rebuild_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(rebuild_group), "System Configuration");
    adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(rebuild_group), "Apply changes to your declarative NixOS environment.");
    
    GtkWidget *test_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(test_row), "Test Configuration");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(test_row), "Build and activate configuration temporarily (lost on reboot).");
    adw_action_row_add_prefix(ADW_ACTION_ROW(test_row), gtk_image_new_from_icon_name("system-run-symbolic"));
    adw_action_row_add_suffix(ADW_ACTION_ROW(test_row), create_action_btn("Test", 4));
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(rebuild_group), test_row);

    GtkWidget *switch_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(switch_row), "Switch Configuration");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(switch_row), "Build, activate, and make configuration the default boot target.");
    adw_action_row_add_prefix(ADW_ACTION_ROW(switch_row), gtk_image_new_from_icon_name("emblem-system-symbolic"));
    adw_action_row_add_suffix(ADW_ACTION_ROW(switch_row), create_action_btn("Switch", 5));
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(rebuild_group), switch_row);

    GtkWidget *rollback_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(rollback_row), "Rollback System");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(rollback_row), "Revert the system to the previous generation instantly.");
    adw_action_row_add_prefix(ADW_ACTION_ROW(rollback_row), gtk_image_new_from_icon_name("edit-undo-symbolic"));
    GtkWidget *btn_roll = create_action_btn("Rollback", 7);
    gtk_widget_add_css_class(btn_roll, "destructive-action");
    adw_action_row_add_suffix(ADW_ACTION_ROW(rollback_row), btn_roll);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(rebuild_group), rollback_row);
    
    gtk_box_append(GTK_BOX(system_margin), rebuild_group);

    GtkWidget *gens_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(gens_group), "Recent Generations");
    adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(gens_group), "Active system profiles stored in /nix/var/nix/profiles/.");
    gens_list = gtk_list_box_new();
    gtk_widget_add_css_class(gens_list, "boxed-list"); 
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(gens_group), gens_list);
    gtk_box_append(GTK_BOX(system_margin), gens_group);

    TaskData *gtask = g_malloc0(sizeof(TaskData)); 
    gtask->action = 8;
    g_thread_new("gens_task", background_task, gtask);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(page_system), system_margin);
    adw_view_stack_add_titled_with_icon(ADW_VIEW_STACK(view_stack), page_system, "system", "System", "computer-symbolic");

    page_search = gtk_scrolled_window_new();
    g_signal_connect(page_search, "edge-reached", G_CALLBACK(on_scroll_edge_reached), NULL);

    GtkWidget *search_margin = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_start(search_margin, 60); gtk_widget_set_margin_end(search_margin, 60); gtk_widget_set_margin_top(search_margin, 20);
    
    GtkWidget *empty_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_valign(empty_box, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(empty_box, 60);
    gtk_widget_set_margin_bottom(empty_box, 60);
    GtkWidget *empty_icon = gtk_image_new_from_icon_name("edit-find-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(empty_icon), 64);
    gtk_widget_add_css_class(empty_icon, "dim-label");
    GtkWidget *empty_label = gtk_label_new("No matches found");
    gtk_widget_add_css_class(empty_label, "title-2");
    gtk_widget_add_css_class(empty_label, "dim-label");
    gtk_box_append(GTK_BOX(empty_box), empty_icon);
    gtk_box_append(GTK_BOX(empty_box), empty_label);
    gtk_list_box_set_placeholder(GTK_LIST_BOX(results_list), empty_box);

    gtk_box_append(GTK_BOX(search_margin), results_list);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(page_search), search_margin);
    gtk_stack_add_named(GTK_STACK(main_stack), page_search, "search");

    page_details = gtk_scrolled_window_new();
    GtkWidget *details_margin = gtk_box_new(GTK_ORIENTATION_VERTICAL, 24);
    gtk_widget_set_margin_start(details_margin, 60); gtk_widget_set_margin_end(details_margin, 60); gtk_widget_set_margin_top(details_margin, 30);
    gtk_widget_set_margin_bottom(details_margin, 40);
    
    GtkWidget *details_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    details_icon_img = gtk_image_new();
    gtk_image_set_pixel_size(GTK_IMAGE(details_icon_img), 96);
    gtk_box_append(GTK_BOX(details_header), details_icon_img);

    GtkWidget *details_title_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_valign(details_title_box, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(details_title_box, TRUE);
    
    details_title = gtk_label_new("");
    gtk_widget_add_css_class(details_title, "title-1");
    gtk_widget_set_halign(details_title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(details_title_box), details_title);
    
    details_dev_lbl = gtk_label_new("");
    gtk_widget_add_css_class(details_dev_lbl, "dim-label");
    gtk_widget_set_halign(details_dev_lbl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(details_title_box), details_dev_lbl);
    
    gtk_box_append(GTK_BOX(details_header), details_title_box);

    GtkWidget *details_action_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_valign(details_action_box, GTK_ALIGN_CENTER);
    
    details_try_btn = gtk_button_new_with_label("Try in Shell");
    gtk_widget_set_size_request(details_try_btn, 120, 44);
    gtk_widget_add_css_class(details_try_btn, "pill");
    g_signal_connect_swapped(details_try_btn, "clicked", G_CALLBACK(trigger_try_shell), details_try_btn);
    gtk_box_append(GTK_BOX(details_action_box), details_try_btn);

    details_install_btn = gtk_button_new_with_label("Install");
    gtk_widget_set_size_request(details_install_btn, 140, 44);
    gtk_widget_add_css_class(details_install_btn, "suggested-action");
    gtk_widget_add_css_class(details_install_btn, "pill");
    g_signal_connect_swapped(details_install_btn, "clicked", G_CALLBACK(trigger_action), details_install_btn);
    gtk_box_append(GTK_BOX(details_action_box), details_install_btn);

    details_store_lbl = gtk_button_new_with_label("Source");
    gtk_widget_add_css_class(details_store_lbl, "flat");
    gtk_widget_set_valign(details_store_lbl, GTK_ALIGN_CENTER);
    
    GtkWidget *install_group = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_append(GTK_BOX(install_group), details_action_box);
    gtk_box_append(GTK_BOX(install_group), details_store_lbl);
    
    gtk_box_append(GTK_BOX(details_header), install_group);
    gtk_box_append(GTK_BOX(details_margin), details_header);

    details_screenshot_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(details_screenshot_box, "screenshot-box");
    details_screenshot_img = gtk_image_new_from_icon_name("image-x-generic-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(details_screenshot_img), 64);
    gtk_widget_set_valign(details_screenshot_img, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(details_screenshot_img, TRUE);
    gtk_box_append(GTK_BOX(details_screenshot_box), details_screenshot_img);
    gtk_box_append(GTK_BOX(details_margin), details_screenshot_box);

    details_desc = gtk_label_new("");
    gtk_label_set_wrap(GTK_LABEL(details_desc), TRUE);
    gtk_widget_set_halign(details_desc, GTK_ALIGN_START);
    gtk_widget_set_valign(details_desc, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(details_margin), details_desc);

    GtkWidget *bento_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(bento_grid), 16);
    gtk_grid_set_column_spacing(GTK_GRID(bento_grid), 16);
    gtk_grid_set_column_homogeneous(GTK_GRID(bento_grid), TRUE);

    version_card = gtk_button_new();
    gtk_widget_add_css_class(version_card, "bento-tile");
    gtk_widget_add_css_class(version_card, "flat");
    gtk_widget_set_hexpand(version_card, TRUE);
    gtk_widget_set_vexpand(version_card, TRUE);
    g_signal_connect(version_card, "clicked", G_CALLBACK(bento_tile_clicked), NULL);
    
    GtkWidget *vc_inner = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    bento_ver_title = gtk_label_new("Version ...");
    gtk_widget_set_halign(bento_ver_title, GTK_ALIGN_START);
    gtk_widget_add_css_class(bento_ver_title, "title-4");
    bento_change_lbl = gtk_label_new("...");
    gtk_widget_set_halign(bento_change_lbl, GTK_ALIGN_START);
    gtk_widget_set_vexpand(bento_change_lbl, TRUE);
    gtk_label_set_wrap(GTK_LABEL(bento_change_lbl), TRUE);
    gtk_label_set_justify(GTK_LABEL(bento_change_lbl), GTK_JUSTIFY_LEFT);
    gtk_box_append(GTK_BOX(vc_inner), bento_ver_title);
    gtk_box_append(GTK_BOX(vc_inner), bento_change_lbl);
    gtk_button_set_child(GTK_BUTTON(version_card), vc_inner);
    gtk_grid_attach(GTK_GRID(bento_grid), version_card, 0, 0, 2, 1);

    bento_safe_icon = gtk_image_new_from_icon_name("dialog-warning-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(bento_safe_icon), 48); 
    tile_2 = create_bento_button(bento_safe_icon, &bento_safe_title, &bento_safe_sub);
    gtk_grid_attach(GTK_GRID(bento_grid), tile_2, 2, 0, 1, 2);

    bento_size_badge = gtk_label_new("...");
    gtk_widget_add_css_class(bento_size_badge, "bento-badge");
    gtk_widget_add_css_class(bento_size_badge, "badge-gray");
    tile_1 = create_bento_button(bento_size_badge, &bento_size_title, &bento_size_sub);
    gtk_label_set_text(GTK_LABEL(bento_size_title), "Download Size");
    gtk_grid_attach(GTK_GRID(bento_grid), tile_1, 0, 1, 1, 1);

    GtkWidget *age_badge = gtk_label_new("All");
    gtk_widget_add_css_class(age_badge, "bento-badge");
    gtk_widget_add_css_class(age_badge, "badge-safe");
    GtkWidget *age_title, *age_sub;
    tile_4 = create_bento_button(age_badge, &age_title, &age_sub);
    gtk_label_set_text(GTK_LABEL(age_title), "Age Rating");
    gtk_label_set_text(GTK_LABEL(age_sub), "General Audience");
    gtk_grid_attach(GTK_GRID(bento_grid), tile_4, 1, 1, 1, 1);

    GtkWidget *desk_icon = gtk_image_new_from_icon_name("computer-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(desk_icon), 32);
    GtkWidget *desk_title, *desk_sub;
    tile_3 = create_horizontal_bento_button(desk_icon, &desk_title, &desk_sub);
    gtk_label_set_text(GTK_LABEL(desk_title), "Hardware Compatibility");
    gtk_label_set_text(GTK_LABEL(desk_sub), "Fully optimized for desktop and laptop environments.");
    gtk_grid_attach(GTK_GRID(bento_grid), tile_3, 0, 2, 3, 1);

    bento_alt_icon = gtk_image_new_from_icon_name("system-software-install-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(bento_alt_icon), 32);
    tile_alt = create_horizontal_bento_button(bento_alt_icon, &bento_alt_title, &bento_alt_sub);
    gtk_grid_attach(GTK_GRID(bento_grid), tile_alt, 0, 3, 3, 1);

    gtk_box_append(GTK_BOX(details_margin), bento_grid);

    GtkWidget *links_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(links_grid), 16);
    gtk_grid_set_column_spacing(GTK_GRID(links_grid), 16);
    gtk_grid_set_column_homogeneous(GTK_GRID(links_grid), TRUE);

    GtkWidget *comm_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(comm_card, "bento-tile");
    gtk_widget_set_valign(comm_card, GTK_ALIGN_START);
    
    GtkWidget *comm_icons_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_halign(comm_icons_box, GTK_ALIGN_CENTER);
    comm_card_icon1 = gtk_image_new_from_icon_name("emblem-favorite-symbolic");
    comm_card_icon2 = gtk_image_new_from_icon_name("system-users-symbolic");
    comm_card_icon3 = gtk_image_new_from_icon_name("emblem-ok-symbolic");
    gtk_widget_add_css_class(comm_card_icon1, "comm-icon");
    gtk_widget_add_css_class(comm_card_icon2, "comm-icon");
    gtk_widget_add_css_class(comm_card_icon3, "comm-icon");
    gtk_box_append(GTK_BOX(comm_icons_box), comm_card_icon1);
    gtk_box_append(GTK_BOX(comm_icons_box), comm_card_icon2);
    gtk_box_append(GTK_BOX(comm_icons_box), comm_card_icon3);
    gtk_box_append(GTK_BOX(comm_card), comm_icons_box);

    comm_card_title = gtk_label_new("Community Built");
    gtk_widget_add_css_class(comm_card_title, "title-4");
    gtk_box_append(GTK_BOX(comm_card), comm_card_title);

    comm_card_desc = gtk_label_new("");
    gtk_widget_add_css_class(comm_card_desc, "dim-label");
    gtk_label_set_wrap(GTK_LABEL(comm_card_desc), TRUE);
    gtk_label_set_justify(GTK_LABEL(comm_card_desc), GTK_JUSTIFY_CENTER);
    gtk_box_append(GTK_BOX(comm_card), comm_card_desc);
    
    comm_card_license = gtk_label_new("");
    gtk_widget_add_css_class(comm_card_license, "accent");
    gtk_box_append(GTK_BOX(comm_card), comm_card_license);

    gtk_grid_attach(GTK_GRID(links_grid), comm_card, 0, 0, 1, 1);

    GtkWidget *links_group = adw_preferences_group_new();
    gtk_widget_set_valign(links_group, GTK_ALIGN_START);

    link_web_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(link_web_row), "Project Website");
    adw_action_row_add_prefix(ADW_ACTION_ROW(link_web_row), gtk_image_new_from_icon_name("applications-internet-symbolic"));
    adw_action_row_add_suffix(ADW_ACTION_ROW(link_web_row), gtk_image_new_from_icon_name("external-link-symbolic"));
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(link_web_row), TRUE);
    g_signal_connect(link_web_row, "activated", G_CALLBACK(on_link_clicked), NULL);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(links_group), link_web_row);

    link_trans_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(link_trans_row), "Contribute Translations");
    adw_action_row_add_prefix(ADW_ACTION_ROW(link_trans_row), gtk_image_new_from_icon_name("preferences-desktop-locale-symbolic"));
    adw_action_row_add_suffix(ADW_ACTION_ROW(link_trans_row), gtk_image_new_from_icon_name("external-link-symbolic"));
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(link_trans_row), TRUE);
    g_signal_connect(link_trans_row, "activated", G_CALLBACK(on_link_clicked), NULL);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(links_group), link_trans_row);

    link_bug_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(link_bug_row), "Report an Issue");
    adw_action_row_add_prefix(ADW_ACTION_ROW(link_bug_row), gtk_image_new_from_icon_name("dialog-warning-symbolic"));
    adw_action_row_add_suffix(ADW_ACTION_ROW(link_bug_row), gtk_image_new_from_icon_name("external-link-symbolic"));
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(link_bug_row), TRUE);
    g_signal_connect(link_bug_row, "activated", G_CALLBACK(on_link_clicked), NULL);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(links_group), link_bug_row);

    link_help_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(link_help_row), "Help");
    adw_action_row_add_prefix(ADW_ACTION_ROW(link_help_row), gtk_image_new_from_icon_name("help-about-symbolic"));
    adw_action_row_add_suffix(ADW_ACTION_ROW(link_help_row), gtk_image_new_from_icon_name("external-link-symbolic"));
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(link_help_row), TRUE);
    g_signal_connect(link_help_row, "activated", G_CALLBACK(on_link_clicked), NULL);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(links_group), link_help_row);

    gtk_grid_attach(GTK_GRID(links_grid), links_group, 1, 0, 1, 1);
    gtk_box_append(GTK_BOX(details_margin), links_grid);

    details_provenance_box = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(details_provenance_box), "Advanced Nix Information");
    
    prov_row_attr = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(prov_row_attr), "System Attribute Map");
    adw_action_row_add_prefix(ADW_ACTION_ROW(prov_row_attr), gtk_image_new_from_icon_name("text-x-generic-symbolic"));
    details_prov_attr = gtk_label_new("");
    gtk_widget_add_css_class(details_prov_attr, "dim-label");
    adw_action_row_add_suffix(ADW_ACTION_ROW(prov_row_attr), details_prov_attr);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(details_provenance_box), prov_row_attr);

    prov_row_arch = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(prov_row_arch), "System Target");
    adw_action_row_add_prefix(ADW_ACTION_ROW(prov_row_arch), gtk_image_new_from_icon_name("cpu-symbolic"));
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(details_provenance_box), prov_row_arch);

    prov_row_store = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(prov_row_store), "Repository");
    adw_action_row_add_prefix(ADW_ACTION_ROW(prov_row_store), gtk_image_new_from_icon_name("folder-symbolic"));
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(details_provenance_box), prov_row_store);
    
    prov_row_license = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(prov_row_license), "License");
    adw_action_row_add_prefix(ADW_ACTION_ROW(prov_row_license), gtk_image_new_from_icon_name("emblem-readonly-symbolic"));
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(details_provenance_box), prov_row_license);

    prov_row_home = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(prov_row_home), "Homepage");
    adw_action_row_add_prefix(ADW_ACTION_ROW(prov_row_home), gtk_image_new_from_icon_name("applications-internet-symbolic"));
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(details_provenance_box), prov_row_home);
    
    gtk_box_append(GTK_BOX(details_margin), details_provenance_box);

    details_nix_code = gtk_label_new("");
    gtk_widget_add_css_class(details_nix_code, "code-box");
    gtk_widget_set_halign(details_nix_code, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(details_margin), details_nix_code);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(page_details), details_margin);
    gtk_stack_add_named(GTK_STACK(main_stack), page_details, "details");

    page_progress = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    gtk_widget_set_margin_start(page_progress, 40); gtk_widget_set_margin_end(page_progress, 40); gtk_widget_set_margin_top(page_progress, 20);
    
    GtkWidget *prog_title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(prog_title), "<span font_size='x-large' font_weight='bold'>System Operation</span>");
    gtk_widget_set_halign(prog_title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(page_progress), prog_title);
    
    progress_bar = gtk_progress_bar_new();
    gtk_box_append(GTK_BOX(page_progress), progress_bar);
    
    GtkWidget *scroll_log = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll_log, TRUE);
    log_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(log_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(log_view), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll_log), log_view);
    
    gtk_box_append(GTK_BOX(page_progress), scroll_log);
    gtk_stack_add_named(GTK_STACK(main_stack), page_progress, "progress");

    gtk_stack_set_visible_child_name(GTK_STACK(main_stack), "view_stack");
    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char *argv[]) {
    AdwApplication *app = adw_application_new("org.nixos.nixpak", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
EOF

echo "=> [4/4] Compiling and Deploying NIXPAK..."
nix-shell -p gcc pkg-config gtk4 libadwaita curl python3 cage bubblewrap --run '
  gcc -O2 nixpkg-core.c -o nixpkg-core || exit 1
  gcc -O2 nixpkg-gui.c -o nixpkg-gui $(pkg-config --cflags --libs gtk4 libadwaita-1) || exit 1
'

install -m 755 nixpak-backend.py /usr/local/bin/nixpak-backend.py
install -m 755 nixpak-backend.sh /usr/local/bin/nixpak-backend.sh
install -m 755 nixpkg-core /usr/local/bin/nixpkg-core
install -m 755 nixpkg-gui /usr/local/bin/nixpkg-gui

cat << 'EOF' > /usr/local/bin/nixpkg
#!/bin/sh
exec pkexec /usr/local/bin/nixpkg-core "$@"
EOF
chmod +x /usr/local/bin/nixpkg

for DIR in /home/*; do
  if [ -d "$DIR" ]; then
    USER_NAME=$(basename "$DIR")
    mkdir -p "$DIR/.local/share/icons/hicolor/256x256/apps"
    if [ -f "ascii-art-text (4).png" ]; then cp "ascii-art-text (4).png" "$DIR/.local/share/icons/hicolor/256x256/apps/nixpak.png"
    elif [ -f "logo.png" ]; then cp "logo.png" "$DIR/.local/share/icons/hicolor/256x256/apps/nixpak.png"; fi
    chmod 644 "$DIR/.local/share/icons/hicolor/256x256/apps/nixpak.png" || true
    mkdir -p "$DIR/.local/share/applications"
    cat << EOF > "$DIR/.local/share/applications/nixpak.desktop"
[Desktop Entry]
Name=NIXPAK
GenericName=Software Center
Comment=A NixOS Power-User Package Manager.
Exec=/usr/local/bin/nixpkg-gui
Icon=nixpak
Type=Application
Terminal=false
Categories=System;PackageManager;
EOF
    chown -R "$USER_NAME:users" "$DIR/.local/share"
    su - "$USER_NAME" -c 'kbuildsycoca6 --noincremental 2>/dev/null || kbuildsycoca5 --noincremental 2>/dev/null' || true
  fi
done

rm -f nixpkg-core.c nixpkg-gui.c nixpak-backend.py nixpak-backend.sh nixpkg-core nixpkg-gui
echo "=> NIXPAK successfully installed! Bwrap Engine perfectly integrated."
