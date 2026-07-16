/*
 =====================================================================
  COCA COFFEE SHOP - 3D WALKTHROUGH (GLUT / legacy OpenGL, C++)
 =====================================================================

  Dibuat berdasarkan denah yang diberikan:
    - Gerbang (gate) di depan
    - Tempat Duduk Lesehan (depan kiri)
    - Tempat Duduk / area kipas (depan kanan)
    - Tempat Duduk (tengah kiri)
    - Pintu Masuk / lobby (tengah kanan, ada pintu kaca ke luar)
    - Lorong kecil kiri & kanan (ruang transisi sempit, sesuai denah)
    - Tempat Duduk Utama (belakang kiri)
    - Tempat Pegawai (belakang kanan, DIBUAT LEBIH SEMPIT/menjorok
      secara denah - tapi tetap di dalam satu bangunan utuh)
    - Gudang / Ruang Tambahan (ruang kecil indoor di sebelah Pegawai,
      mengisi sisa proporsi dari denah - bangunan tetap persegi penuh,
      beratap & berlantai menyatu, TIDAK berlubang/terbuka ke luar)

  Prinsip grafika komputer yang diterapkan:
    - MVP: Model (glTranslate/glRotate per objek), View (kamera FPS
      dengan gluLookAt manual dari posisi+yaw+pitch), Projection
      (gluPerspective)
    - Lighting: GL_LIGHT0 sebagai cahaya matahari/ambient global,
      GL_LIGHT1..GL_LIGHT6 sebagai lampu tiap ruangan (point light)
    - Texturing: seluruh permukaan (lantai/dinding/langit-langit/atap/
      pintu kaca/rumput) memakai tekstur prosedural (dibuat lewat
      kode, tidak butuh file gambar eksternal) supaya langsung bisa
      di-compile tanpa dependency tambahan
    - Depth testing, backface culling, smooth shading, fog ringan

  Kamu bisa jalan masuk lewat gerbang -> tempat duduk lesehan, lalu
  menjelajah semua ruangan lewat pintu-pintu yang sudah dibuka sesuai
  denah. Interior (meja/kursi/dekorasi detail) SENGAJA masih minim -
  tinggal kamu arahkan mau ditambah apa di tiap ruangan.

 =====================================================================
  CARA COMPILE
 =====================================================================
  Linux (Ubuntu/Debian):
    sudo apt-get install freeglut3-dev libglu1-mesa-dev libgl1-mesa-dev
    g++ coca_coffee.cpp -o coca -lglut -lGLU -lGL
    ./coca

  Windows (MinGW + freeglut):
    g++ coca_coffee.cpp -o coca.exe -lfreeglut -lopengl32 -lglu32
    coca.exe

 =====================================================================
  KONTROL
 =====================================================================
    W A S D      : jalan maju/kiri/mundur/kanan
    Mouse         : melihat sekeliling (klik dulu di window)
    Shift         : lari
    ESC           : keluar
    F1            : tampilkan/sembunyikan HUD info ruangan
 =====================================================================
*/

#include <GL/glut.h>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>

// ---------------------------------------------------------------
// KONSTANTA DUNIA / DENAH
// ---------------------------------------------------------------
const float WALL_H      = 4.0f;   // tinggi dinding
const float WALL_T      = 0.25f;  // tebal dinding
const float DOOR_H      = 2.6f;   // tinggi bukaan pintu
const float EYE_H       = 1.7f;   // tinggi mata pemain
const float PLAYER_R    = 0.35f;  // radius collision pemain

// batas keseluruhan bangunan (X kiri-kanan, Z depan-belakang)
const float BX0 = 0.0f,  BX1 = 24.0f;
const float BZ0 = 0.0f,  BZ1 = 34.0f;
const float MIDX = 12.0f;
const float ROWZ1 = 11.0f;
const float ROWZ2 = 20.0f;   // batas ruang tengah <-> lorong kecil
const float ROWZ3 = 23.0f;   // batas lorong kecil <-> baris belakang
const float PEGX0 = 16.0f;   // Tempat Pegawai lebih sempit (menjorok), tidak selebar kolom kanan di atasnya

// ---------------------------------------------------------------
// TEXTURE IDS
// ---------------------------------------------------------------
GLuint texWood, texTile, texWall, texGlass, texCeil, texRoof, texGrass, texBrick, texCounter, texArt;

// ---------------------------------------------------------------
// STRUKTUR RUANGAN (untuk HUD + penempatan lampu)
// ---------------------------------------------------------------
struct Room {
    const char* name;
    float x0, x1, z0, z1;
    float lightPos[3];
};

std::vector<Room> rooms = {
    { "Tempat Duduk Lesehan",     0, 12, 0, 11,  {6, 3.5f, 5.5f} },
    { "Tempat Duduk (Kipas)",    12, 24, 0, 11,  {18,3.5f, 5.5f} },
    { "Tempat Duduk",             0, 12, 11,20,  {6, 3.5f,15.5f} },
    { "Pintu Masuk / Lobby",     12, 24, 11,20,  {18,3.5f,15.5f} },
    { "Tempat Duduk Utama",       0, 12, 23,34,  {6, 3.5f,28.5f} },
    { "Tempat Pegawai",          16, 24, 23,34,  {20,3.5f,28.5f} },
    { "Gudang / Ruang Tambahan", 12, 16, 23,34,  {14,3.5f,28.5f} },
};

// Ruang kecil / lorong tambahan (tidak dikasih lampu plafon sendiri -
// cukup terang dari cahaya ruangan sebelah + ambient), dipakai untuk
// label HUD saja.
struct SubRoom { const char* name; float x0,x1,z0,z1; };
std::vector<SubRoom> subRooms = {
    { "Lorong Kecil (kiri)",   0,12, 20,23 },
    { "Lorong Kecil (kanan)", 12,24, 20,23 },
};

// ---------------------------------------------------------------
// COLLISION: daftar AABB dinding (xmin,xmax,zmin,zmax)
// ---------------------------------------------------------------
struct WallBox { float x0,x1,z0,z1; };
std::vector<WallBox> colliders;

void addCollider(float x0,float x1,float z0,float z1){
    colliders.push_back({x0,x1,z0,z1});
}

// ---------------------------------------------------------------
// KAMERA
// ---------------------------------------------------------------
struct Camera {
    float x = 6.0f, y = EYE_H, z = -3.0f; // mulai di luar gerbang
    float yaw = 90.0f;   // menghadap +Z (masuk ke dalam)
    float pitch = 0.0f;
} cam;

bool keyDown[256] = {false};
bool specialLeft=false, specialRight=false, specialUp=false, specialDown=false;
bool mouseCaptured = true;
int winW = 1000, winH = 700;
bool warping = false;
bool showHUD = true;
bool shiftHeld = false;
std::string currentRoomName = "Di Luar / Gerbang";

// ---------------------------------------------------------------
// UTIL: buat tekstur prosedural dari fungsi warna per-pixel
// ---------------------------------------------------------------
template<typename F>
GLuint makeTexture(int size, F colorFunc){
    std::vector<unsigned char> data(size*size*3);
    for(int y=0;y<size;y++){
        for(int x=0;x<size;x++){
            unsigned char r,g,b;
            colorFunc(x,y,size,r,g,b);
            int idx=(y*size+x)*3;
            data[idx+0]=r; data[idx+1]=g; data[idx+2]=b;
        }
    }
    GLuint id;
    glGenTextures(1,&id);
    glBindTexture(GL_TEXTURE_2D,id);
    gluBuild2DMipmaps(GL_TEXTURE_2D,GL_RGB,size,size,GL_RGB,GL_UNSIGNED_BYTE,data.data());
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    return id;
}

float frand(){ return (float)rand()/(float)RAND_MAX; }

void initTextures(){
    // lantai kayu (garis-garis papan horizontal, coklat)
    texWood = makeTexture(128, [](int x,int y,int size,unsigned char&r,unsigned char&g,unsigned char&b){
        int plank = (y / 16) % 2;
        float base = 0.45f + 0.06f*sinf(x*0.25f) + (plank?0.05f:0.0f);
        float noise = (frand()-0.5f)*0.03f;
        r = (unsigned char)std::min(255.0f,std::max(0.0f,(base+0.10f+noise)*255));
        g = (unsigned char)std::min(255.0f,std::max(0.0f,(base-0.05f+noise)*180));
        b = (unsigned char)std::min(255.0f,std::max(0.0f,(base-0.20f+noise)*90));
        if(y%16==0){ r=r*6/10; g=g*6/10; b=b*6/10; }
    });
    // lantai tegel/keramik (checker abu-abu terang)
    texTile = makeTexture(128, [](int x,int y,int size,unsigned char&r,unsigned char&g,unsigned char&b){
        int cx=(x/32)%2, cy=(y/32)%2;
        bool light = (cx==cy);
        unsigned char base = light?232:210;
        bool grout = (x%32==0)||(y%32==0);
        r=g=b= grout? 160 : base;
    });
    // dinding (cat krem dengan sedikit noise plester)
    texWall = makeTexture(64, [](int x,int y,int size,unsigned char&r,unsigned char&g,unsigned char&b){
        float n = (frand()-0.5f)*10.0f;
        r=(unsigned char)std::min(255.0f,std::max(0.0f,235+n));
        g=(unsigned char)std::min(255.0f,std::max(0.0f,225+n));
        b=(unsigned char)std::min(255.0f,std::max(0.0f,205+n));
    });
    // kaca pintu masuk (biru muda transparan-ish, garis frame)
    texGlass = makeTexture(64, [](int x,int y,int size,unsigned char&r,unsigned char&g,unsigned char&b){
        bool frame = (x<3)||(x>size-4)||(y<3)||(y>size-4)||(x>size/2-2&&x<size/2+2)||(y>size/2-2&&y<size/2+2);
        if(frame){ r=90;g=70;b=50; }
        else { r=190;g=215;b=225; }
    });
    // langit-langit (putih dengan grid panel)
    texCeil = makeTexture(64, [](int x,int y,int size,unsigned char&r,unsigned char&g,unsigned char&b){
        bool grid = (x%32<2)||(y%32<2);
        unsigned char base = grid?210:245;
        r=g=b=base;
    });
    // atap (genteng merah gelap bergaris)
    texRoof = makeTexture(64, [](int x,int y,int size,unsigned char&r,unsigned char&g,unsigned char&b){
        int row=(y/8);
        bool line=(y%8==0);
        r = line? 90 : 150+ (row%2)*10;
        g = line? 30 : 60;
        b = line? 25 : 45;
    });
    // rumput/halaman luar (hijau bervariasi)
    texGrass = makeTexture(64, [](int x,int y,int size,unsigned char&r,unsigned char&g,unsigned char&b){
        float n=frand()*30.0f;
        r=(unsigned char)(60+n*0.3f);
        g=(unsigned char)(120+n);
        b=(unsigned char)(50+n*0.3f);
    });
    // bata gerbang
    texBrick = makeTexture(64, [](int x,int y,int size,unsigned char&r,unsigned char&g,unsigned char&b){
        int row=y/8;
        int off=(row%2)*8;
        bool mortar=((x+off)%16==0)||(y%8==0);
        if(mortar){ r=200;g=195;b=185; }
        else { r=150+ (rand()%20); g=70; b=55; }
    });
    // kayu meja/counter (lebih gelap dari lantai)
    texCounter = makeTexture(64, [](int x,int y,int size,unsigned char&r,unsigned char&g,unsigned char&b){
        float base = 0.35f + 0.05f*sinf(x*0.3f);
        r=(unsigned char)(base*180);
        g=(unsigned char)(base*110);
        b=(unsigned char)(base*60);
        if(y%10==0){ r=r*7/10; g=g*7/10; b=b*7/10; }
    });
    // galeri lukisan (mosaik kotak warna-warni + bingkai hitam, terinspirasi
    // dinding galeri pop-art di foto referensi)
    texArt = makeTexture(128, [](int x,int y,int size,unsigned char&r,unsigned char&g,unsigned char&b){
        int cols=4, rows=5;
        int cellW=size/cols, cellH=size/rows;
        int cx=x/cellW, cy=y/cellH;
        bool border=(x%cellW<3)||(y%cellH<3)||(x%cellW>cellW-4)||(y%cellH>cellH-4);
        if(border){ r=g=b=18; return; }
        float seed = sinf(cx*12.9898f+cy*78.233f)*43758.5453f;
        float frac = seed - floorf(seed);
        float rr=0.5f+0.5f*sinf(frac*6.28318f);
        float gg=0.5f+0.5f*sinf(frac*6.28318f+2.094f);
        float bb=0.5f+0.5f*sinf(frac*6.28318f+4.188f);
        r=(unsigned char)(rr*230+15);
        g=(unsigned char)(gg*230+15);
        b=(unsigned char)(bb*230+15);
    });
}

// ---------------------------------------------------------------
// PRIMITIF GAMBAR: kotak bertekstur dengan normal per-face
// ---------------------------------------------------------------
void drawBox(float x0,float x1,float y0,float y1,float z0,float z1, GLuint tex, float texScale=1.0f, bool solidCollider=true){
    float sx=(x1-x0)*texScale, sy=(y1-y0)*texScale, sz=(z1-z0)*texScale;
    glBindTexture(GL_TEXTURE_2D, tex);
    glBegin(GL_QUADS);
        // depan (-Z)
        glNormal3f(0,0,-1);
        glTexCoord2f(0,0); glVertex3f(x0,y0,z0);
        glTexCoord2f(sx,0); glVertex3f(x1,y0,z0);
        glTexCoord2f(sx,sy); glVertex3f(x1,y1,z0);
        glTexCoord2f(0,sy); glVertex3f(x0,y1,z0);
        // belakang (+Z)
        glNormal3f(0,0,1);
        glTexCoord2f(0,0); glVertex3f(x1,y0,z1);
        glTexCoord2f(sx,0); glVertex3f(x0,y0,z1);
        glTexCoord2f(sx,sy); glVertex3f(x0,y1,z1);
        glTexCoord2f(0,sy); glVertex3f(x1,y1,z1);
        // kiri (-X)
        glNormal3f(-1,0,0);
        glTexCoord2f(0,0); glVertex3f(x0,y0,z1);
        glTexCoord2f(sz,0); glVertex3f(x0,y0,z0);
        glTexCoord2f(sz,sy); glVertex3f(x0,y1,z0);
        glTexCoord2f(0,sy); glVertex3f(x0,y1,z1);
        // kanan (+X)
        glNormal3f(1,0,0);
        glTexCoord2f(0,0); glVertex3f(x1,y0,z0);
        glTexCoord2f(sz,0); glVertex3f(x1,y0,z1);
        glTexCoord2f(sz,sy); glVertex3f(x1,y1,z1);
        glTexCoord2f(0,sy); glVertex3f(x1,y1,z0);
        // bawah (-Y)
        glNormal3f(0,-1,0);
        glTexCoord2f(0,0); glVertex3f(x0,y0,z0);
        glTexCoord2f(sx,0); glVertex3f(x1,y0,z0);
        glTexCoord2f(sx,sz); glVertex3f(x1,y0,z1);
        glTexCoord2f(0,sz); glVertex3f(x0,y0,z1);
        // atas (+Y)
        glNormal3f(0,1,0);
        glTexCoord2f(0,0); glVertex3f(x0,y1,z1);
        glTexCoord2f(sx,0); glVertex3f(x1,y1,z1);
        glTexCoord2f(sx,sz); glVertex3f(x1,y1,z0);
        glTexCoord2f(0,sz); glVertex3f(x0,y1,z0);
    glEnd();

    if(solidCollider) addCollider(x0,x1,z0,z1);
}

// quad datar (untuk lantai luas / langit-langit luas tanpa collider)
void drawFlatQuad(float x0,float x1,float y,float z0,float z1,GLuint tex,float texScale,bool up){
    float sx=(x1-x0)*texScale, sz=(z1-z0)*texScale;
    glBindTexture(GL_TEXTURE_2D,tex);
    glBegin(GL_QUADS);
    if(up){
        glNormal3f(0,1,0);
        glTexCoord2f(0,0); glVertex3f(x0,y,z1);
        glTexCoord2f(sx,0); glVertex3f(x1,y,z1);
        glTexCoord2f(sx,sz); glVertex3f(x1,y,z0);
        glTexCoord2f(0,sz); glVertex3f(x0,y,z0);
    } else {
        glNormal3f(0,-1,0);
        glTexCoord2f(0,0); glVertex3f(x0,y,z0);
        glTexCoord2f(sx,0); glVertex3f(x1,y,z0);
        glTexCoord2f(sx,sz); glVertex3f(x1,y,z1);
        glTexCoord2f(0,sz); glVertex3f(x0,y,z1);
    }
    glEnd();
}

// ---------------------------------------------------------------
// DINDING DENGAN BUKAAN PINTU (gap)
// gapA>=gapB berarti tidak ada bukaan (dinding utuh)
// ---------------------------------------------------------------
void wallAlongX(float z, float x0, float x1, float gapA, float gapB, GLuint tex){
    float z0=z-WALL_T*0.5f, z1=z+WALL_T*0.5f;
    if(gapA>=gapB){
        drawBox(x0,x1,0,WALL_H,z0,z1,texWall);
        return;
    }
    if(gapA>x0) drawBox(x0,gapA,0,WALL_H,z0,z1,texWall);
    if(gapB<x1) drawBox(gapB,x1,0,WALL_H,z0,z1,texWall);
    // header di atas pintu
    drawBox(gapA,gapB,DOOR_H,WALL_H,z0,z1,texWall,1.0f,false);
    // area bukaan tetap collider kosong (tidak ditambahkan)
}

void wallAlongZ(float x, float z0, float z1, float gapA, float gapB, GLuint tex){
    float x0=x-WALL_T*0.5f, x1=x+WALL_T*0.5f;
    if(gapA>=gapB){
        drawBox(x0,x1,0,WALL_H,z0,z1,texWall);
        return;
    }
    if(gapA>z0) drawBox(x0,x1,0,WALL_H,z0,gapA,texWall);
    if(gapB<z1) drawBox(x0,x1,0,WALL_H,gapB,z1,texWall);
    drawBox(x0,x1,DOOR_H,WALL_H,gapA,gapB,texWall,1.0f,false);
}

// deklarasi awal (dipakai oleh drawFrontSignage() di bawah, definisi
// lengkapnya ada di bagian "LABEL NAMA RUANGAN")
void drawLabel3D(float x,float y,float z,const char* text);

// pintu kaca (dekorasi non-collider, dipasang persis di bukaan)
void glassDoor(float x, float z0,float z1,float y0,float y1){
    glBindTexture(GL_TEXTURE_2D, texGlass);
    glBegin(GL_QUADS);
        glNormal3f(1,0,0);
        glTexCoord2f(0,0); glVertex3f(x,y0,z0);
        glTexCoord2f(1,0); glVertex3f(x,y0,z1);
        glTexCoord2f(1,1); glVertex3f(x,y1,z1);
        glTexCoord2f(0,1); glVertex3f(x,y1,z0);
    glEnd();
}

// ---------------------------------------------------------------
// DEKORASI / FURNITURE - terinspirasi foto interior asli yang dikirim
// (counter, meja-kursi kayu, lampion, arc lamp, cermin, galeri lukisan,
// tanaman, kisi-kisi kayu, signage depan). Semua dibuat dari primitif
// sederhana (box/sphere) + hierarchical transform (push/pop matrix),
// tidak perlu model 3D eksternal.
// ---------------------------------------------------------------

// kotak warna solid tanpa tekstur (dipakai untuk bantal/daun/aksen warna)
void drawColorBox(float x0,float x1,float y0,float y1,float z0,float z1, float r,float g,float b){
    glDisable(GL_TEXTURE_2D);
    glColor3f(r,g,b);
    drawBox(x0,x1,y0,y1,z0,z1, texWall, 1.0f, false);
    glEnable(GL_TEXTURE_2D);
    glColor3f(1,1,1);
}

// dinding seni datar (quad tekstur galeri, nempel di permukaan dinding
// menghadap +X, non-collider - sama gayanya dengan glassDoor())
void drawGalleryWall(float x, float z0,float z1,float y0,float y1){
    glBindTexture(GL_TEXTURE_2D, texArt);
    glBegin(GL_QUADS);
        glNormal3f(1,0,0);
        glTexCoord2f(0,0); glVertex3f(x,y0,z0);
        glTexCoord2f(1,0); glVertex3f(x,y0,z1);
        glTexCoord2f(1,1); glVertex3f(x,y1,z1);
        glTexCoord2f(0,1); glVertex3f(x,y1,z0);
    glEnd();
}

// kursi kayu bergaya Skandinavia (seat + backrest + 4 kaki), koordinat
// lokal relatif ke pusatnya sendiri - diposisikan lewat push/pop matrix
void drawChair(float cx, float cz, float facingDeg){
    glPushMatrix();
    glTranslatef(cx, 0.0f, cz);
    glRotatef(facingDeg, 0,1,0);
    drawBox(-0.22f,0.22f, 0.42f,0.47f, -0.20f,0.20f, texCounter, 1.0f, false);   // dudukan
    drawBox(-0.20f,0.20f, 0.47f,0.85f,  0.16f,0.20f, texCounter, 1.0f, false);   // sandaran
    drawBox(-0.20f,-0.14f, 0.0f,0.42f, -0.18f,-0.12f, texCounter,1.0f,false);    // kaki
    drawBox( 0.14f, 0.20f, 0.0f,0.42f, -0.18f,-0.12f, texCounter,1.0f,false);
    drawBox(-0.20f,-0.14f, 0.0f,0.42f,  0.12f, 0.18f, texCounter,1.0f,false);
    drawBox( 0.14f, 0.20f, 0.0f,0.42f,  0.12f, 0.18f, texCounter,1.0f,false);
    glPopMatrix();
}

// meja kayu persegi + 4 kaki
void drawTable(float cx, float cz, float w, float d){
    glPushMatrix();
    glTranslatef(cx,0.0f,cz);
    drawBox(-w/2,w/2, 0.72f,0.78f, -d/2,d/2, texCounter,1.0f,false);
    float lx=w/2-0.06f, lz=d/2-0.06f;
    drawBox(-lx-0.03f,-lx+0.03f, 0,0.72f, -lz-0.03f,-lz+0.03f, texCounter,1,false);
    drawBox( lx-0.03f, lx+0.03f, 0,0.72f, -lz-0.03f,-lz+0.03f, texCounter,1,false);
    drawBox(-lx-0.03f,-lx+0.03f, 0,0.72f,  lz-0.03f, lz+0.03f, texCounter,1,false);
    drawBox( lx-0.03f, lx+0.03f, 0,0.72f,  lz-0.03f, lz+0.03f, texCounter,1,false);
    glPopMatrix();
}

// satu set meja + 4 kursi (dining set kayu, seperti di foto lounge/patio)
void drawDiningSet(float cx, float cz, float rot){
    drawTable(cx,cz, 1.0f,0.7f);
    drawChair(cx, cz-0.55f, rot+0.0f);
    drawChair(cx, cz+0.55f, rot+180.0f);
    drawChair(cx-0.65f, cz, rot+90.0f);
    drawChair(cx+0.65f, cz, rot-90.0f);
}

// lampion kertas merah gantung (seperti foto lounge & counter)
void drawLantern(float cx, float ceilingY, float cz, float dropLen){
    float bottomY = ceilingY - dropLen;
    drawColorBox(cx-0.008f,cx+0.008f, bottomY, ceilingY, cz-0.008f,cz+0.008f, 0.15f,0.15f,0.15f); // tali
    glPushMatrix();
    glTranslatef(cx, bottomY-0.05f, cz);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glColor3f(0.92f,0.22f,0.14f);
    glutSolidSphere(0.22, 16,16);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_LIGHTING);
    glColor3f(1,1,1);
    glPopMatrix();
}

// lampu lantai lengkung (arc lamp), seperti di foto lounge utama
void drawArcLamp(float cx, float cz){
    glPushMatrix();
    glTranslatef(cx,0.0f,cz);
    drawBox(-0.18f,0.18f, 0,0.05f, -0.18f,0.18f, texWall,1.0f,false); // alas
    drawBox(-0.03f,0.03f, 0.05f,1.7f, -0.03f,0.03f, texWall,1.0f,false); // tiang
    glTranslatef(0,1.7f,0);
    glRotatef(-35.0f, 0,0,1); // condong (lengkung)
    drawBox(-0.03f,0.03f, 0,1.1f, -0.03f,0.03f, texWall,1.0f,false); // lengan miring
    glTranslatef(0,1.1f,0.05f);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glColor3f(0.82f,0.82f,0.78f);
    glPushMatrix();
    glScalef(1.0f,0.55f,1.0f);
    glutSolidSphere(0.24,16,16); // kap lampu
    glPopMatrix();
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_LIGHTING);
    glColor3f(1,1,1);
    glPopMatrix();
}

// cermin oval berbingkai kayu, nempel di dinding menghadap +X
void drawMirror(float x, float y0,float y1, float z0,float z1){
    drawBox(x-0.04f,x, y0-0.06f,y1+0.06f, z0-0.06f,z1+0.06f, texCounter,1.0f,false); // bingkai
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glColor3f(0.72f,0.78f,0.82f);
    glBegin(GL_QUADS);
        glNormal3f(1,0,0);
        glVertex3f(x+0.001f,y0,z0);
        glVertex3f(x+0.001f,y0,z1);
        glVertex3f(x+0.001f,y1,z1);
        glVertex3f(x+0.001f,y1,z0);
    glEnd();
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_LIGHTING);
    glColor3f(1,1,1);
}

// tanaman pot (daun bulat hijau + pot kayu)
void drawPottedPlant(float cx, float cz, float scale){
    drawBox(cx-0.15f*scale,cx+0.15f*scale, 0,0.3f*scale, cz-0.15f*scale,cz+0.15f*scale, texCounter,1.0f,false);
    glPushMatrix();
    glTranslatef(cx, 0.3f*scale, cz);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glColor3f(0.22f,0.48f,0.20f);
    glutSolidSphere(0.32*scale, 10,10);
    glTranslatef(0.05f*scale, 0.28f*scale, 0.02f*scale);
    glutSolidSphere(0.20*scale, 10,10);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_LIGHTING);
    glColor3f(1,1,1);
    glPopMatrix();
}

// partisi kisi-kisi kayu (seperti sekat di foto area semi-outdoor)
void drawLatticeScreen(float x0,float x1,float y0,float y1,float z){
    int cols=6, rows=4;
    float cw=(x1-x0)/cols, rh=(y1-y0)/rows;
    float bar=0.05f;
    for(int i=0;i<=cols;i++){
        float x=x0+i*cw;
        drawBox(x-bar/2,x+bar/2, y0,y1, z-0.03f,z+0.03f, texCounter,1.0f,false);
    }
    for(int j=0;j<=rows;j++){
        float y=y0+j*rh;
        drawBox(x0,x1, y-bar/2,y+bar/2, z-0.03f,z+0.03f, texCounter,1.0f,false);
    }
}

// counter/bar kayu + meja marmer + etalase kaca sederhana (seperti foto 2)
void drawCoffeeCounter(float x0,float x1, float z0,float z1){
    drawBox(x0,x1, 0,1.0f, z0,z1, texCounter, 0.8f);              // badan counter (kayu)
    drawColorBox(x0-0.03f,x1+0.03f, 1.0f,1.05f, z0-0.03f,z1+0.03f, 0.92f,0.92f,0.90f); // top marmer putih
    // etalase kaca kecil di salah satu ujung
    float gx0=x0+0.3f, gx1=x0+1.3f;
    drawBox(gx0,gx1, 1.05f,1.55f, z0+0.15f,z1-0.15f, texGlass, 1.0f, false);
    // mesin kopi (kotak hitam kecil di ujung lain)
    drawColorBox(x1-1.0f,x1-0.4f, 1.05f,1.45f, z0+0.2f,z1-0.2f, 0.12f,0.12f,0.12f);
}

// dais/panggung kayu rendah untuk area lesehan + meja pendek
void drawLesehanArea(float x0,float x1,float z0,float z1){
    drawBox(x0,x1, 0,0.18f, z0,z1, texCounter, 0.5f, false);   // panggung rendah
    float cx=(x0+x1)/2.0f, cz=(z0+z1)/2.0f;
    drawBox(cx-0.55f,cx+0.55f, 0.18f,0.40f, cz-0.35f,cz+0.35f, texCounter,1.0f,false); // meja pendek
    drawColorBox(cx-0.9f,cx-0.5f, 0.18f,0.30f, cz-0.6f,cz-0.2f, 0.75f,0.30f,0.28f); // bantal
    drawColorBox(cx+0.5f,cx+0.9f, 0.18f,0.30f, cz-0.6f,cz-0.2f, 0.30f,0.45f,0.55f);
    drawColorBox(cx-0.9f,cx-0.5f, 0.18f,0.30f, cz+0.2f,cz+0.6f, 0.85f,0.65f,0.25f);
    drawColorBox(cx+0.5f,cx+0.9f, 0.18f,0.30f, cz+0.2f,cz+0.6f, 0.35f,0.55f,0.35f);
}

// kipas angin plafon sederhana (poros + 4 bilah datar)
void drawCeilingFan(float cx, float cz, float ceilingY){
    drawColorBox(cx-0.04f,cx+0.04f, ceilingY-0.35f, ceilingY, cz-0.04f,cz+0.04f, 0.1f,0.1f,0.1f); // poros
    float y=ceilingY-0.38f;
    drawColorBox(cx-0.9f,cx+0.9f, y,y+0.03f, cz-0.06f,cz+0.06f, 0.15f,0.13f,0.10f);
    drawColorBox(cx-0.06f,cx+0.06f, y,y+0.03f, cz-0.9f,cz+0.9f, 0.15f,0.13f,0.10f);
}

// papan nama "COCA" + tanaman + kursi luar di gerbang (seperti foto 6)
void drawFrontSignage(){
    drawColorBox(4.6f,7.4f, 3.15f,3.55f, -1.86f,-1.79f, 0.96f,0.95f,0.92f); // plakat putih
    drawLabel3D(5.15f, 3.62f, -1.82f, "COCA");
    drawPottedPlant(2.6f, -2.3f, 1.2f);
    drawPottedPlant(9.4f, -2.3f, 1.2f);
    drawDiningSet(2.0f, -6.5f, 90.0f);
}

// ---------------------------------------------------------------
// LABEL NAMA RUANGAN MENGAMBANG (untuk orientasi sementara)
// ---------------------------------------------------------------
void drawLabel3D(float x,float y,float z,const char* text){
    glPushAttrib(GL_LIGHTING_BIT | GL_ENABLE_BIT);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glColor3f(0.1f,0.1f,0.1f);
    glRasterPos3f(x,y,z);
    for(const char* c=text; *c; c++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18,*c);
    glPopAttrib();
}

// ---------------------------------------------------------------
// BANGUN SELURUH STRUKTUR (dipanggil tiap frame - sederhana karena
// scene statis, tidak perlu display list untuk kejelasan kode)
// ---------------------------------------------------------------
void buildBuilding(){
    colliders.clear();

    // ===================== LANTAI PER RUANGAN =====================
    drawFlatQuad(0,12, 0.0f, 0,11, texWood, 0.4f, true);     // Lesehan
    drawFlatQuad(12,24,0.0f, 0,11, texTile, 0.3f, true);     // Kipas
    drawFlatQuad(0,12, 0.0f, 11,20,texWood, 0.4f, true);     // Duduk tengah
    drawFlatQuad(12,24,0.0f, 11,20,texTile, 0.3f, true);     // Lobby
    drawFlatQuad(0,12, 0.0f, 20,23,texWood, 0.4f, true);     // Lorong kecil kiri
    drawFlatQuad(12,24,0.0f, 20,23,texTile, 0.3f, true);     // Lorong kecil kanan
    drawFlatQuad(0,12, 0.0f, 23,34,texWood, 0.4f, true);     // Duduk Utama
    drawFlatQuad(16,24,0.0f, 23,34,texTile, 0.3f, true);     // Pegawai (lebih sempit)
    drawFlatQuad(12,16,0.0f, 23,34,texWood, 0.4f, true);     // Gudang / Ruang Tambahan (indoor, bukan taman terbuka)

    // ===================== LANGIT-LANGIT (satu bidang utuh, bangunan tidak berlubang) =====================
    drawFlatQuad(0,24, WALL_H, 0,34, texCeil, 0.4f, false);

    // ===================== ATAP (dari luar, satu blok utuh) =====================
    drawBox(-0.5f,24.5f, WALL_H+0.05f, WALL_H+0.35f, -0.5f,34.5f, texRoof, 0.6f, false);

    // ===================== DINDING LUAR =====================
    // Utara (z=0): bukaan gerbang x 4-8 menuju Lesehan
    wallAlongX(BZ0, BX0,BX1, 4,8, texWall);
    // Selatan (z=34): solid penuh (bangunan utuh, tidak ada notch keluar)
    wallAlongX(BZ1, BX0,BX1, 0,0, texWall);
    // Barat (x=0): solid penuh
    wallAlongZ(BX0, BZ0,BZ1, 0,0, texWall);
    // Timur (x=24): bukaan pintu masuk kaca z 13-17
    wallAlongZ(BX1, BZ0,BZ1, 13,17, texWall);
    glassDoor(BX1, 13,17, 0, DOOR_H);

    // ===================== DINDING DALAM =====================
    // Baris 1 / Baris 2 (z = 11)
    wallAlongX(ROWZ1, 0,12, 4,8, texWall);      // Lesehan <-> Duduk tengah
    wallAlongX(ROWZ1, 12,24, 16,20, texWall);   // Kipas <-> Lobby
    // Baris 2 / Lorong kecil (z = 20)
    wallAlongX(ROWZ2, 0,12, 4,8, texWall);      // Duduk tengah <-> Lorong kiri
    wallAlongX(ROWZ2, 12,24, 16,20, texWall);   // Lobby <-> Lorong kanan
    // Lorong kecil / Baris belakang (z = 23)
    wallAlongX(ROWZ3, 0,12, 4,8, texWall);        // Lorong kiri <-> Duduk Utama
    wallAlongX(ROWZ3, 12,PEGX0, 13,15, texWall);  // Lorong kanan <-> Gudang (pintu)
    wallAlongX(ROWZ3, PEGX0,24, 18,22, texWall);  // Lorong kanan <-> Pegawai

    // Kolom kiri/kanan (x = 12)
    // baris1 (z0-11): open-plan, TIDAK ada dinding (lesehan & kipas menyatu)
    wallAlongZ(MIDX, 11,20, 14,18, texWall);    // Duduk tengah <-> Lobby
    wallAlongZ(MIDX, 20,23, 0,0, texWall);      // Lorong kiri <-> Lorong kanan (dipisah, tidak nyambung)
    wallAlongZ(MIDX, 23,34, 24,26, texWall);    // Duduk Utama <-> Gudang (bukaan counter)
    // Kolom pemisah Gudang <-> Pegawai (x = 16)
    wallAlongZ(PEGX0, 23,34, 0,0, texWall);     // solid (Pegawai hanya diakses lewat lorong kanan)

    // ===================== GERBANG DEPAN =====================
    drawBox(3.0f,4.0f, 0,3.2f, -3.0f,-1.8f, texBrick);   // pilar kiri
    drawBox(8.0f,9.0f, 0,3.2f, -3.0f,-1.8f, texBrick);   // pilar kanan
    drawBox(3.0f,9.0f, 3.2f,3.6f, -3.0f,-1.8f, texBrick,1.0f,false); // balok atas gerbang
    drawFrontSignage();   // papan nama "COCA" + tanaman + meja outdoor (foto 6)

    // ===================== HALAMAN LUAR (rumput) =====================
    drawFlatQuad(-15,39, -0.01f, -15,45, texGrass, 0.25f, true);

    // ===================== INTERIOR: Tempat Duduk Lesehan (foto 4 - galeri lukisan) =====================
    drawLesehanArea(2.0f,10.0f, 2.0f,9.0f);
    drawGalleryWall(0.0f, 2.5f,8.5f, 0.9f,3.6f);          // dinding galeri di sisi barat
    drawLantern(6.0f, WALL_H, 5.5f, 1.3f);

    // ===================== INTERIOR: Tempat Duduk / area kipas (foto 5 - semi-outdoor) =====================
    drawCeilingFan(18.0f, 5.5f, WALL_H);
    drawLatticeScreen(18.5f,23.5f, 0.0f,3.2f, 23.9f);      // kisi kayu dekat dinding lorong
    drawPottedPlant(19.5f, 2.0f, 1.0f);
    drawPottedPlant(22.5f, 2.0f, 1.0f);
    drawDiningSet(15.5f, 6.5f, 20.0f);

    // ===================== INTERIOR: Pintu Masuk / Lobby (foto 2 - counter/bar) =====================
    drawCoffeeCounter(14.0f,22.0f, 12.0f,13.2f);
    drawLantern(19.0f, WALL_H, 16.5f, 1.1f);
    drawPottedPlant(13.0f, 12.6f, 0.9f);

    // ===================== INTERIOR: Tempat Duduk Utama (foto 3 - lounge utama) =====================
    drawDiningSet(4.0f, 26.5f, 0.0f);
    drawDiningSet(8.0f, 31.0f, 15.0f);
    drawArcLamp(2.2f, 30.5f);
    drawMirror(0.0f, 0.9f,2.1f, 24.5f,26.0f);
    drawLantern(6.0f, WALL_H, 28.5f, 1.2f);

    // ===================== DEKORASI MINIMAL PENANDA RUANGAN =====================
    // counter kayu sederhana di ruang duduk utama, bukaan takeaway ke Gudang
    drawBox(9.6f,11.8f, 0,1.0f, 24,26, texCounter);

    // ===================== LABEL NAMA RUANGAN (mengambang, sementara) =====================
    for(auto &r: rooms){
        float cx=(r.x0+r.x1)/2.0f, cz=(r.z0+r.z1)/2.0f;
        drawLabel3D(cx-1.5f, 2.6f, cz, r.name);
    }
    for(auto &r: subRooms){
        float cx=(r.x0+r.x1)/2.0f, cz=(r.z0+r.z1)/2.0f;
        drawLabel3D(cx-1.5f, 2.2f, cz, r.name);
    }
}

// ---------------------------------------------------------------
// LIGHTING SETUP
// ---------------------------------------------------------------
void setupLights(){
    glEnable(GL_LIGHTING);

    // LIGHT0 = matahari / ambient umum, dari atas sedikit condong (masuk lewat gerbang & pintu kaca)
    GLfloat sunPos[]  = {5.0f, 20.0f, -10.0f, 1.0f};
    GLfloat sunAmb[]  = {0.28f,0.28f,0.32f,1.0f};
    GLfloat sunDiff[] = {0.55f,0.53f,0.48f,1.0f};
    glLightfv(GL_LIGHT0, GL_POSITION, sunPos);
    glLightfv(GL_LIGHT0, GL_AMBIENT,  sunAmb);
    glLightfv(GL_LIGHT0, GL_DIFFUSE,  sunDiff);
    glEnable(GL_LIGHT0);

    // LIGHT1..LIGHT7 = lampu langit-langit tiap ruangan (point light, redup, hangat)
    for(size_t i=0;i<rooms.size() && i<7;i++){
        GLenum id = GL_LIGHT1 + (GLenum)i;
        GLfloat pos[4] = { rooms[i].lightPos[0], rooms[i].lightPos[1], rooms[i].lightPos[2], 1.0f };
        GLfloat diff[4]= {0.55f,0.48f,0.35f,1.0f};
        GLfloat spec[4]= {0.3f,0.3f,0.25f,1.0f};
        glLightfv(id, GL_POSITION, pos);
        glLightfv(id, GL_DIFFUSE, diff);
        glLightfv(id, GL_SPECULAR, spec);
        glLightf(id, GL_CONSTANT_ATTENUATION, 1.0f);
        glLightf(id, GL_LINEAR_ATTENUATION, 0.06f);
        glLightf(id, GL_QUADRATIC_ATTENUATION, 0.02f);
        glEnable(id);
    }
}

// ---------------------------------------------------------------
// COLLISION HELPERS
// ---------------------------------------------------------------
bool collidesAt(float x, float z){
    for(auto &w: colliders){
        if(x + PLAYER_R > w.x0 && x - PLAYER_R < w.x1 &&
           z + PLAYER_R > w.z0 && z - PLAYER_R < w.z1)
            return true;
    }
    return false;
}

void tryMove(float dx, float dz){
    float nx = cam.x + dx;
    float nz = cam.z + dz;
    if(!collidesAt(nx, cam.z)) cam.x = nx;
    if(!collidesAt(cam.x, nz)) cam.z = nz;
}

void updateCurrentRoom(){
    if(cam.z < 0){ currentRoomName = "Di Luar / Gerbang"; return; }
    for(auto &r: rooms){
        if(cam.x>=r.x0 && cam.x<r.x1 && cam.z>=r.z0 && cam.z<r.z1){
            currentRoomName = r.name; return;
        }
    }
    for(auto &r: subRooms){
        if(cam.x>=r.x0 && cam.x<r.x1 && cam.z>=r.z0 && cam.z<r.z1){
            currentRoomName = r.name; return;
        }
    }
    currentRoomName = "Lorong";
}

// ---------------------------------------------------------------
// INPUT
// ---------------------------------------------------------------
void keyDownFunc(unsigned char key,int,int){
    keyDown[key]=true;
    shiftHeld = (glutGetModifiers() & GLUT_ACTIVE_SHIFT) != 0;
    if(key==27) exit(0); // ESC
}
void keyUpFunc(unsigned char key,int,int){
    keyDown[key]=false;
    shiftHeld = (glutGetModifiers() & GLUT_ACTIVE_SHIFT) != 0;
}

void specialKeyDown(int key,int,int){
    shiftHeld = (glutGetModifiers() & GLUT_ACTIVE_SHIFT) != 0;
    if(key==GLUT_KEY_F1) showHUD=!showHUD;
}

void mouseMotion(int x,int y){
    if(warping){ warping=false; return; }
    shiftHeld = (glutGetModifiers() & GLUT_ACTIVE_SHIFT) != 0;
    float dx = (float)(x - winW/2);
    float dy = (float)(y - winH/2);
    cam.yaw   += dx * 0.15f;
    cam.pitch -= dy * 0.15f;
    if(cam.pitch>89.0f) cam.pitch=89.0f;
    if(cam.pitch<-89.0f) cam.pitch=-89.0f;
    if(mouseCaptured){
        warping = true;
        glutWarpPointer(winW/2, winH/2);
    }
    glutPostRedisplay();
}

void updateMovement(){
    float speed = 0.10f;
    if(shiftHeld) speed = 0.20f;

    float radYaw = cam.yaw * (float)M_PI/180.0f;
    float fwdX = cosf(radYaw), fwdZ = sinf(radYaw);
    float rightX = cosf(radYaw - (float)M_PI/2.0f), rightZ = sinf(radYaw - (float)M_PI/2.0f);

    float dx=0, dz=0;
    if(keyDown['w']||keyDown['W']){ dx += fwdX*speed;  dz += fwdZ*speed; }
    if(keyDown['s']||keyDown['S']){ dx -= fwdX*speed;  dz -= fwdZ*speed; }
    if(keyDown['a']||keyDown['A']){ dx -= rightX*speed; dz -= rightZ*speed; }
    if(keyDown['d']||keyDown['D']){ dx += rightX*speed; dz += rightZ*speed; }

    if(dx!=0 || dz!=0) tryMove(dx,dz);

    // batasi tidak keluar dari halaman
    if(cam.x < -14) cam.x=-14;
    if(cam.x > 38)  cam.x=38;
    if(cam.z < -14) cam.z=-14;
    if(cam.z > 44)  cam.z=44;

    updateCurrentRoom();
}

// ---------------------------------------------------------------
// HUD (overlay 2D)
// ---------------------------------------------------------------
void drawHUD(){
    glMatrixMode(GL_PROJECTION);
    glPushMatrix(); glLoadIdentity();
    gluOrtho2D(0,winW,0,winH);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix(); glLoadIdentity();

    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);

    glColor3f(1,1,1);
    char buf[256];
    snprintf(buf,sizeof(buf), "Ruangan: %s", currentRoomName.c_str());
    glRasterPos2i(15, winH-25);
    for(char* c=buf; *c; c++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18,*c);

    const char* help = "WASD jalan | Mouse lihat sekeliling | Shift lari | F1 toggle HUD | ESC keluar";
    glRasterPos2i(15, 20);
    for(const char* c=help; *c; c++) glutBitmapCharacter(GLUT_BITMAP_9_BY_15,*c);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_TEXTURE_2D);

    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW); glPopMatrix();
}

// ---------------------------------------------------------------
// DISPLAY / RESHAPE / TIMER
// ---------------------------------------------------------------
void display(){
    glClearColor(0.55f,0.70f,0.85f,1.0f); // langit
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // ---------- PROJECTION ----------
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(70.0, (double)winW/(double)winH, 0.05, 200.0);

    // ---------- VIEW (kamera FPS manual, bagian dari MVP) ----------
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    float radYaw   = cam.yaw   * (float)M_PI/180.0f;
    float radPitch = cam.pitch * (float)M_PI/180.0f;
    float lx = cosf(radYaw)*cosf(radPitch);
    float ly = sinf(radPitch);
    float lz = sinf(radYaw)*cosf(radPitch);
    gluLookAt(cam.x, cam.y, cam.z,
              cam.x+lx, cam.y+ly, cam.z+lz,
              0,1,0);

    setupLights();

    // fog ringan biar interior terasa berkedalaman
    glEnable(GL_FOG);
    GLfloat fogColor[4] = {0.55f,0.70f,0.85f,1.0f};
    glFogfv(GL_FOG_COLOR, fogColor);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, 15.0f);
    glFogf(GL_FOG_END, 60.0f);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glShadeModel(GL_SMOOTH);
    glColor3f(1,1,1);

    // ---------- MODEL (semua objek bangunan, static di world space) ----------
    buildBuilding();

    glDisable(GL_FOG);

    if(showHUD) drawHUD();

    glutSwapBuffers();
}

void reshape(int w,int h){
    winW = w>1?w:1; winH = h>1?h:1;
    glViewport(0,0,winW,winH);
}

void timerFunc(int){
    updateMovement();
    glutPostRedisplay();
    glutTimerFunc(16, timerFunc, 0);
}

void mouseClick(int, int, int, int){
    mouseCaptured = true;
    glutSetCursor(GLUT_CURSOR_NONE);
}

void initGL(){
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_NORMALIZE);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    initTextures();
}

int main(int argc,char** argv){
    glutInit(&argc,argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
    glutInitWindowSize(winW,winH);
    glutCreateWindow("Coca Coffee Shop - 3D Walkthrough (GLUT)");

    initGL();

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyDownFunc);
    glutKeyboardUpFunc(keyUpFunc);
    glutSpecialFunc(specialKeyDown);
    glutPassiveMotionFunc(mouseMotion);
    glutMouseFunc(mouseClick);
    glutSetCursor(GLUT_CURSOR_NONE);
    glutTimerFunc(16, timerFunc, 0);

    glutMainLoop();
    return 0;
}
