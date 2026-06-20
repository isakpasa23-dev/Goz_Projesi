#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>

// =========================================================================
// 1. EKRAN VE DONANIM TANIMLAMALARI
// =========================================================================
#define SCREEN_WIDTH 128   // OLED ekran genişliği (piksel)
#define SCREEN_HEIGHT 64   // OLED ekran yüksekliği (piksel)
#define OLED_RESET -1      // Reset pini (paylaşılıyorsa veya yoksa -1)

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// =========================================================================
// 2. HAREKET VE BEKLEME PARAMETRELERİ
// =========================================================================
#define minbekleme 3000    // Sabit hedefe bakarken minimum bekleme süresi (ms)
#define maxbekleme 6000    // Sabit hedefe bakarken maksimum bekleme süresi (ms)

// Gözlerin hedefe yaklaşma hızını kontrol eden fren katsayıları.
// Değer büyüdükçe gözler daha yavaş/yumuşak hareket eder (interpolasyon yumuşar).
unsigned int solFrenKatsayisi = 5; 
unsigned int sagFrenKatsayisi = 5; 

int gozKoseYumusatma = 4; // fillRoundRect için köşe yuvarlatma yarıçapı (piksel)

// =========================================================================
// 3. GÖZ VERİ YAPISI VE BAŞLANGIÇ DURUMLARI
// =========================================================================
struct Goz {
  float x;                // Anlık X koordinatı (göz bebeği merkezi)
  float y;                // Anlık Y koordinatı (göz bebeği merkezi)
  
  float dikey;            // Anlık dikey boyutu (yükseklik)
  float yatay;            // Anlık yatay boyutu (genişlik)
  
  float hedefDikey;       // Duygu durumuna göre hedeflenen dikey boyut
  float hedefYatay;       // Duygu durumuna göre hedeflenen yatay boyut
  
  float kirpmaEskiDikey;  // Göz kırparken eski dikey boyutu korumak için geçici değişken
  
  float kasInme;          // Anlık kaş inme miktarı (üst kısımdan çizilen maske)
  float hedefKasInme;     // Hedeflenen kaş inme miktarı
};

// Sol ve Sağ göz başlangıç durumları
// {x, y, dikey, yatay, hedefDikey, hedefYatay, kirpmaEskiDikey, kasInme, hedefKasInme}
Goz solGoz = {32, 32, 32, 21, 32, 21, 32, 0, 0};
Goz sagGoz = {96, 32, 32, 21, 32, 21, 32, 0, 0};

// =========================================================================
// 4. GÖZ KIRPMA SİSTEMİ DEĞİŞKENLERİ
// =========================================================================
byte kirpmaDurumu = 0;             // 0: Kırpmıyor, 1: Kapanıyor, 2: Açılıyor
unsigned long kirpmaAsamaBaslangic = 0; // Aşamanın (kapanma/açılma) başladığı milisaniye
unsigned int kapanmaSuresi = 0;         // Gözün kapanma animasyon süresi (ms)
unsigned int acilmaSuresi = 0;          // Gözün açılma animasyon süresi (kapanmanın 2 katı)
unsigned long sonKirpmaZamani = 0;      // En son göz kırpılan zaman
unsigned int sonrakiKirpmaSuresi = 3000;// İki kırpma arasındaki rastgele bekleme süresi
unsigned int kalanKirpmaSayisi = 0;     // Çoklu kırpma (multi-blink) için kalan kırpma adedi
bool kisilarakHedefDegisiyor = false;   // Hareket sırasında göz kısılarak hedef değiştiriyorsa true

// =========================================================================
// 5. ODAKLANMA VE DUYGU DEĞİŞKENLERİ
// =========================================================================
int ortakhedefX = 64;     // Gözlerin odaklandığı ortak X hedefi (ekran ortası varsayılan)
int ortakhedefY = 32;     // Gözlerin odaklandığı ortak Y hedefi (ekran ortası varsayılan)
int gozAraligi = 32;      // İki gözün merkezleri arasındaki yatay mesafe
byte mevcutDuygu = 0;     // 0: NORMAL, 1: KISMA, 2: ŞAŞKIN, 3: KIZGIN

// Duygu geçişlerini yumuşatmak ve ani değişimleri engellemek için hafıza ağırlıkları
float kismaEtkisi = 0;
float saskinEtkisi = 0;
float kizginEtkisi = 0;

// Duyguya bağlı olarak dinamik güncellenen kırpma süreleri
int minKirpmaSuresi = 2000;
int maxKirpmaSuresi = 5000;
int gozKirparakHareketSans = 70; // Büyük hareketlerde göz kırpma tetiklenme şansı (%)

// =========================================================================
// 6. İNSANSILAŞTIRMA VE DOĞALLIK ETMENLERİ
// =========================================================================
int limitDikeyKirpma = 1;         // Kırpma esnasında gözün ineceği minimum dikey boyut (1: tam, >1: yarım)
int solGozKirpmaGecikmesi = 0;     // Sol gözün kırpma gecikmesi (ms, asimetri için)
int sagGozKirpmaGecikmesi = 0;     // Sağ gözün kırpma gecikmesi (ms, asimetri için)

// Duygu değişim zamanlayıcısı
unsigned long sonDuyguZamani = 0;
unsigned int beklemeSuresiDuygu = 4000;

// Saccade (Göz titremesi / mikro hareketler) için offset değerleri
int saccadeX = 0;
int saccadeY = 0;

// Göz hedefe vardı mı kontrolü
bool hedefeVardi = false;          
unsigned long hedefeVarmaZamani = 0; 
unsigned int beklemeSuresi = 0;    

// =========================================================================
// 7. GÖZ KIRPMA TETİKLEYİCİSİ (ASİMETRİ VE YARIM KIRPMA HESAPLARI)
// =========================================================================
void gozKirpTetikle() {
  if (kirpmaDurumu == 0) {
    kirpmaDurumu = 1; // Kapanma aşamasına geç
    kirpmaAsamaBaslangic = millis();
    kapanmaSuresi = random(50, 150); 
    acilmaSuresi = kapanmaSuresi * 2; // Açılma süresi kesinlikle kapanmanın 2 katı (2x kuralı)

    // Yarım Kırpma Kontrolü: %25 ihtimalle gözler tam kapanmaz (12-18 piksel aralığında kalır)
    if (random(100) < 25) {
      limitDikeyKirpma = random(12, 18);
    } else {
      limitDikeyKirpma = 1;
    }

    // Milisaniyelik Asimetrik Kırpma: Sol ve sağ göz arasında 3-8 ms arası rastgele bir gecikme yaratılır
    int gecikme = random(3, 9);
    if (random(2) == 0) {
      solGozKirpmaGecikmesi = gecikme;
      sagGozKirpmaGecikmesi = 0;
    } else {
      solGozKirpmaGecikmesi = 0;
      sagGozKirpmaGecikmesi = gecikme;
    }

    // Mevcut dikey boyutları kırpma öncesi hafızaya al
    solGoz.kirpmaEskiDikey = solGoz.hedefDikey;
    sagGoz.kirpmaEskiDikey = sagGoz.hedefDikey; 

    kisilarakHedefDegisiyor = true;
  }
}

// =========================================================================
// 8. ODAK VE DUYGUYA GÖRE HEDEF ŞEKİL GÜNCELLEMELERİ
// =========================================================================
void gozHedefleriniGuncelle() {
  // Gözlerin kendi içinde hafif asimetrik durması için ufak rastgele sapmalar
  int temelD = random(-1, 2); 
  int temelY = random(-1, 2); 

  int solRastgeleD = temelD;
  int solRastgeleY = temelY;
  int sagRastgeleD = temelD;
  int sagRastgeleY = temelY;

  int bozuklukTipi = random(3);

  // Gözlerin tamamen simetrik ve yapay durmaması için ufak boyut farklılıkları üretilir
  if (bozuklukTipi == 0) {
    solRastgeleD += random(-3, 4);
    solRastgeleY += random(-2, 3);
  } 
  else if (bozuklukTipi == 1) {
    sagRastgeleD += random(-3, 4);
    sagRastgeleY += random(-2, 3);
  } 
  else {
    solRastgeleD += random(-2, 3);
    solRastgeleY += random(-1, 2);
    sagRastgeleD += random(-2, 3);
    sagRastgeleY += random(-1, 2);
  }

  // Dikey ve yatay asimetrilerin birbirine çok uzak olmaması için sınırlandırma (Maks 3 piksel)
  if (abs(solRastgeleD - sagRastgeleD) > 3) {
    sagRastgeleD = (solRastgeleD > sagRastgeleD) ? (solRastgeleD - 3) : (solRastgeleD + 3);
  }
  if (abs(solRastgeleY - sagRastgeleY) > 3) {
    sagRastgeleY = (solRastgeleY > sagRastgeleY) ? (solRastgeleY - 3) : (solRastgeleY + 3);
  }

  // Gözün ekrandaki pozisyonuna göre odak derinliği offseti hesaplanır.
  // Göz kenarlara baktıkça odaklanmadan dolayı boyutlarında hafif bir küçülme veya büyüme olur.
  int dx = ortakhedefX - 64;
  int dy = ortakhedefY - 32;
  float dist = sqrt(dx * dx + dy * dy);
  float odakOffset = (dist * 0.077778f) - 1.5f; // (dist / 45.0) * 3.5 hesaplamasının optimize edilmiş hali

  switch (mevcutDuygu) {
    case 0: // --- NORMAL DUYGU ---
      solGoz.hedefYatay = max(10.0, 21 + solRastgeleY + odakOffset);  
      solGoz.hedefDikey = max(10.0, 32 + solRastgeleD + odakOffset);
      sagGoz.hedefYatay = max(10.0, 21 + sagRastgeleY + odakOffset);  
      sagGoz.hedefDikey = max(10.0, 32 + sagRastgeleD + odakOffset);
      solGoz.hedefKasInme = 0; 
      sagGoz.hedefKasInme = 0; 
      gozAraligi = random(15, 28); // Göz aralığı üst sınırı azaltıldı (Maks 28)
      break;

    case 1: // --- KISIK / YORGUN DUYGU ---
      solGoz.hedefYatay = max(10.0, 21 + solRastgeleY + (odakOffset * 0.5f));  
      solGoz.hedefDikey = max(8.0, 16 + solRastgeleD + (odakOffset * 0.5f));
      sagGoz.hedefYatay = max(10.0, 21 + sagRastgeleY + (odakOffset * 0.5f));  
      sagGoz.hedefDikey = max(8.0, 16 + sagRastgeleD + (odakOffset * 0.5f));
      solGoz.hedefKasInme = 0; 
      sagGoz.hedefKasInme = 0; 
      gozAraligi = random(15, 22); // Göz aralığı üst sınırı azaltıldı (Maks 22)
      break;

    case 2: // --- ŞAŞKIN DUYGU ---
      solGoz.hedefYatay = max(15.0, 32 + solRastgeleY + odakOffset);  
      solGoz.hedefDikey = max(20.0, 42 + solRastgeleD + odakOffset);
      sagGoz.hedefYatay = max(15.0, 32 + sagRastgeleY + odakOffset);  
      sagGoz.hedefDikey = max(20.0, 42 + sagRastgeleD + odakOffset);
      solGoz.hedefKasInme = 0; 
      sagGoz.hedefKasInme = 0; 
      gozAraligi = random(20, 32); // Göz aralığı üst sınırı azaltıldı (Maks 32)
      break;

    case 3: // --- KIZGIN DUYGU ---
      solGoz.hedefYatay = 28 + solRastgeleY;  
      solGoz.hedefDikey = 28 + solRastgeleD;
      sagGoz.hedefYatay = 28 + sagRastgeleY;  
      sagGoz.hedefDikey = 28 + sagRastgeleD;
      solGoz.hedefKasInme = 16; // Kaşların kızgınlık maskesi için aşağı inme miktarı
      sagGoz.hedefKasInme = 16; 
      gozAraligi = random(12, 18); // Göz aralığı üst sınırı azaltıldı (Maks 18)
      break;
  }
}

// =========================================================================
// 9. AĞIRLIKLI RASGELE DUYGU SEÇİMİ
// =========================================================================
byte duyguSec() {
  // Gözlerin tam ortaya odaklanıp odaklanmadığı kontrolü
  bool ortada = (ortakhedefX >= 48 && ortakhedefX <= 62) && 
                (ortakhedefY >= 9 && ortakhedefY <= 23);

  // Bakış yönüne göre duyguların baz ağırlıkları belirlenir
  int normalAgirlik = ortada ? 900 : 920;
  int kizginAgirlik = ortada ? 75 : 2;
  int kismaAgirlik  = ortada ? 20 : 63;
  int saskinAgirlik = ortada ? 5 : 15;

  int toplamNormal = normalAgirlik;
  
  // Duygu hafızasındaki değerler ağırlıklara eklenir
  int toplamKisma = kismaAgirlik + (int)kismaEtkisi;
  if (toplamKisma < 0) toplamKisma = 0;

  int toplamKizgin = kizginAgirlik + (int)kizginEtkisi;
  if (toplamKizgin < 0) toplamKizgin = 0;

  int toplamSaskin = saskinAgirlik + (int)saskinEtkisi;
  if (toplamSaskin < 0) toplamSaskin = 0;

  int toplamHavuz = toplamNormal + toplamKisma + toplamKizgin + toplamSaskin;
  if (toplamHavuz <= 0) toplamHavuz = 1; 

  int r = random(toplamHavuz);

  // Seçilen rastgele sayıya göre yeni duygu döndürülür
  if (r < toplamNormal) return 0; // NORMAL
  else if (r < toplamNormal + toplamKisma) return 1; // KISMA
  else if (r < toplamNormal + toplamKisma + toplamSaskin) return 2; // ŞAŞKIN
  else return 3; // KIZGIN
}

// =========================================================================
// 10. SETUP (BAŞLANGIÇ EKRANI VE DONANIM KURULUMU)
// =========================================================================
void setup() {
  Serial.begin(9600);

  // Analog gürültü kullanılarak güçlü ve benzersiz bir random seed (çekirdek) üretilir
  unsigned long gercekRastgele = 0;
  for (int i = 0; i < 15; i++) {
    gercekRastgele += analogRead(A0); 
    delay(2);
  }
  randomSeed(gercekRastgele + micros());

  // OLED Ekran başlatma
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    while (1); // Ekran yoksa kodu kilitle
  }

  // Başlangıç Açılış Ekranı (Centering & Formatting)
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  
  // "GOZ" kelimesini ortalayarak yazıyoruz (Boyut: 2)
  display.setTextSize(2);
  display.setCursor(47, 11);
  display.println(F("GOZ"));
  
  // "PROJESI" kelimesini ortalayarak yazıyoruz (Boyut: 2)
  display.setCursor(23, 29);
  display.println(F("PROJESI"));
  
  // "V1.5" sürüm bilgisini en alta ortalayarak yazıyoruz (Boyut: 1)
  display.setTextSize(1);
  display.setCursor(53, 47);
  display.println(F("V1.5"));
  
  display.display();
  delay(2000); // 2 saniye boyunca açılış ekranını göster
}

// =========================================================================
// 11. ANA DÖNGÜ (MAIN LOOP)
// =========================================================================
void loop() {
  display.clearDisplay(); 

  // -----------------------------------------------------------------------
  // ADIM 1: ANLIK KONUM HESAPLAMALARI (Dinamik Hedefe Yönelme)
  // -----------------------------------------------------------------------
  int solHedefX = (ortakhedefX + saccadeX) - gozAraligi;
  int sagHedefX = (ortakhedefX + saccadeX) + gozAraligi;
  int ortakY = ortakhedefY + saccadeY;

  // Yumuşak geçiş formülü: Mevcut konum = Mevcut + (Hedef - Mevcut) / FrenKatsayisi
  solGoz.x += ((solHedefX - solGoz.x) / solFrenKatsayisi);
  solGoz.y += ((ortakY - solGoz.y) / solFrenKatsayisi);

  sagGoz.x += ((sagHedefX - sagGoz.x) / sagFrenKatsayisi);
  sagGoz.y += ((ortakY - sagGoz.y) / sagFrenKatsayisi);

  // -----------------------------------------------------------------------
  // ADIM 2: BOYUT VE KAŞ HAREKET HESAPLAMALARI
  // -----------------------------------------------------------------------
  // Göz kırpma animasyonu çalışmıyorken dikey boyutu hedefe doğru süzündür
  if (kirpmaDurumu == 0) {
    solGoz.dikey += (solGoz.hedefDikey - solGoz.dikey) / solFrenKatsayisi;
    sagGoz.dikey += (sagGoz.hedefDikey - sagGoz.dikey) / sagFrenKatsayisi;
  }
  solGoz.yatay += (solGoz.hedefYatay - solGoz.yatay) / solFrenKatsayisi;
  sagGoz.yatay += (sagGoz.hedefYatay - sagGoz.yatay) / sagFrenKatsayisi;

  solGoz.kasInme += (solGoz.hedefKasInme - solGoz.kasInme) / solFrenKatsayisi;
  sagGoz.kasInme += (sagGoz.hedefKasInme - sagGoz.kasInme) / sagFrenKatsayisi;

  // -----------------------------------------------------------------------
  // ADIM 3: HEDEFE VARMA VE MİKRO HAREKET (SACCADE) KONTROLLERİ
  // -----------------------------------------------------------------------
  // Tüm parametreler hedefine 1 pikselden daha fazla yaklaştıysa hedefe varıldı kabul edilir
  bool tamam = 
    fabs(solHedefX - solGoz.x) < 1 &&
    fabs(sagHedefX - sagGoz.x) < 1 &&
    fabs(ortakY - solGoz.y) < 1 &&
    fabs(ortakY - sagGoz.y) < 1 &&
    fabs(solGoz.dikey - solGoz.hedefDikey) < 1 &&
    fabs(solGoz.yatay - solGoz.hedefYatay) < 1 &&
    fabs(solGoz.kasInme - solGoz.hedefKasInme) < 1;

  if (tamam && !hedefeVardi) {
    hedefeVardi = true;           
    hedefeVarmaZamani = millis(); 
    beklemeSuresi = random(minbekleme, maxbekleme); 
  }

  if (hedefeVardi) {
    // Saccade Kontrolü: Göz sabit dururken %4 şansla mikro düzeyde titreme/seğirme yapar
    if (random(1000) < 40) { 
      saccadeX += random(-1, 2); 
      saccadeY += random(-1, 2); 

      saccadeX = constrain(saccadeX, -3, 3); 
      saccadeY = constrain(saccadeY, -3, 3);
    }

    // Kızgınken tek gözün istemsizce seğirmesi (%1.5 şans)
    if (mevcutDuygu == 3 && random(1000) < 15) {
      if (random(2) == 0) solGoz.dikey -= random(2, 4);
      else sagGoz.dikey -= random(2, 4);
    }
  } else {
    saccadeX = 0;
    saccadeY = 0;
  }

  // -----------------------------------------------------------------------
  // ADIM 4: GÖZ KIRPMA ANİMASYON DÖNGÜSÜ
  // -----------------------------------------------------------------------
  if (kirpmaDurumu == 0 && (millis() - sonKirpmaZamani > sonrakiKirpmaSuresi)) {
      kirpmaDurumu = 1; // Kapanma aşamasını başlat
      kirpmaAsamaBaslangic = millis();

      kapanmaSuresi = random(50, 150); // Kapanma hızı (50-150ms)
      acilmaSuresi = kapanmaSuresi * 2; // Açılma süresi kesinlikle 2 katı (2x)   

      // Yarım Kırpma Kontrolü (%25 Şansla yarım kapatır)
      if (random(100) < 25) {
        limitDikeyKirpma = random(12, 18);
      } else {
        limitDikeyKirpma = 1;
      }

      // Milisaniyelik Asimetrik Kırpma Farkı (3-8ms)
      int gecikme = random(3, 9);
      if (random(2) == 0) {
        solGozKirpmaGecikmesi = gecikme;
        sagGozKirpmaGecikmesi = 0;
      } else {
        solGozKirpmaGecikmesi = 0;
        sagGozKirpmaGecikmesi = gecikme;
      }

      solGoz.kirpmaEskiDikey = solGoz.hedefDikey;
      sagGoz.kirpmaEskiDikey = sagGoz.hedefDikey; 

      // Çoklu Kırpma Tetikleme Mekanizması
      if (kalanKirpmaSayisi == 0) {
        int r_blink = random(1000);
        int multiSans = random(5, 13);    
        int doubleSans = random(40, 61);  
        
        if (r_blink < multiSans) {
          kalanKirpmaSayisi = random(3, 8); // 3 ile 7 kez peş peşe kırpma
          kismaEtkisi = 8000;               // Kırpma krizi sonrası yorgun/kısık kalma eğilimi
        } else if (r_blink < (doubleSans + multiSans)) {
          kalanKirpmaSayisi = 1;            // Çift göz kırpma
        } else {
          kalanKirpmaSayisi = 0;            // Tek göz kırpma
        }
      }
  }

  // --- Kapanma Aşaması ---
  if (kirpmaDurumu == 1) { 
    unsigned long t = millis() - kirpmaAsamaBaslangic;
    int limitDikey = kisilarakHedefDegisiyor ? 8 : limitDikeyKirpma; 

    // Sol göz kapanma interpolasyonu (asimetrik gecikmeli)
    long t_sol = (long)t - solGozKirpmaGecikmesi;
    if (t_sol < 0) t_sol = 0;
    float oran_sol = constrain((float)t_sol / kapanmaSuresi, 0.0, 1.0);
    solGoz.dikey = solGoz.kirpmaEskiDikey - (solGoz.kirpmaEskiDikey - limitDikey) * oran_sol;

    // Sağ göz kapanma interpolasyonu (asimetrik gecikmeli)
    long t_sag = (long)t - sagGozKirpmaGecikmesi;
    if (t_sag < 0) t_sag = 0;
    float oran_sag = constrain((float)t_sag / kapanmaSuresi, 0.0, 1.0);
    sagGoz.dikey = sagGoz.kirpmaEskiDikey - (sagGoz.kirpmaEskiDikey - limitDikey) * oran_sag;

    if (t_sol >= kapanmaSuresi && t_sag >= kapanmaSuresi) {
      solGoz.dikey = limitDikey;
      sagGoz.dikey = limitDikey;
      kirpmaDurumu = 2; // Açılma aşamasına geç
      kirpmaAsamaBaslangic = millis();
    }
  } 
  // --- Açılma Aşaması ---
  else if (kirpmaDurumu == 2) { 
    unsigned long t = millis() - kirpmaAsamaBaslangic;
    int limitDikey = kisilarakHedefDegisiyor ? 8 : limitDikeyKirpma;

    // Sol göz açılma interpolasyonu (asimetrik gecikmeli + Quadratic Ease-Out)
    long t_sol = (long)t - solGozKirpmaGecikmesi;
    if (t_sol < 0) t_sol = 0;
    float oran_sol = constrain((float)t_sol / acilmaSuresi, 0.0, 1.0);
    oran_sol = 1.0 - (1.0 - oran_sol) * (1.0 - oran_sol); // Yumuşak kas gevşeme formülü
    solGoz.dikey = limitDikey + (solGoz.kirpmaEskiDikey - limitDikey) * oran_sol;

    // Sağ göz açılma interpolasyonu (asimetrik gecikmeli + Quadratic Ease-Out)
    long t_sag = (long)t - sagGozKirpmaGecikmesi;
    if (t_sag < 0) t_sag = 0;
    float oran_sag = constrain((float)t_sag / acilmaSuresi, 0.0, 1.0);
    oran_sag = 1.0 - (1.0 - oran_sag) * (1.0 - oran_sag); // Yumuşak kas gevşeme formülü
    sagGoz.dikey = limitDikey + (sagGoz.kirpmaEskiDikey - limitDikey) * oran_sag;

    if (t_sol >= acilmaSuresi && t_sag >= acilmaSuresi) {
      solGoz.dikey = solGoz.kirpmaEskiDikey;
      sagGoz.dikey = sagGoz.kirpmaEskiDikey; 
      kirpmaDurumu = 0; 
      sonKirpmaZamani = millis(); 
      kisilarakHedefDegisiyor = false; 

      if (kalanKirpmaSayisi > 0) {
        kalanKirpmaSayisi--;
        sonrakiKirpmaSuresi = random(60, 200); // Çoklu kırpma aralarındaki çok kısa süre
      } else {
        sonrakiKirpmaSuresi = random(minKirpmaSuresi, maxKirpmaSuresi) + random(-500, 1501); 
        if (sonrakiKirpmaSuresi < 1000) sonrakiKirpmaSuresi = 1000;
      }
    }
  }

  // -----------------------------------------------------------------------
  // ADIM 5: BAĞIMSIZ HAREKET ZAMANLAYICI KONTROLLERİ
  // -----------------------------------------------------------------------
  if (hedefeVardi && (millis() - hedefeVarmaZamani > beklemeSuresi)) {
    hedefeVardi = false; 
    beklemeSuresi = random(minbekleme, maxbekleme); 

    int hareketTipi = random(100); 

    if (hareketTipi < 70) { // --- BÜYÜK HAREKET (Saccadic Shift) ---
      ortakhedefX = random(35, 76);
      ortakhedefY = random(2, 31);
      
      // Mevcut duyguya göre hedefe varış fren katsayıları atanır
      if (mevcutDuygu == 2) {       // ŞAŞKIN (Çok hızlı tepki)
        solFrenKatsayisi = random(3, 6);
      } else if (mevcutDuygu == 1) { // KISMA (Yorgun ve uyuşuk)
        solFrenKatsayisi = random(8, 14);
      } else if (mevcutDuygu == 3) { // KIZGIN (Agresif ve hızlı)
        solFrenKatsayisi = random(3, 7);
      } else {                       // NORMAL
        solFrenKatsayisi = random(4, 10);
      }
      sagFrenKatsayisi = solFrenKatsayisi + random(-1, 2);
    } 
    else { // --- MİKRO SÜZME (Gözün yavaşça kayması) ---
      ortakhedefX = constrain(ortakhedefX + random(-5, 6), 35, 76);
      ortakhedefY = constrain(ortakhedefY + random(-4, 5), 2, 31);
      
      if (mevcutDuygu == 2) {
        solFrenKatsayisi = random(10, 15);
      } else if (mevcutDuygu == 1) {
        solFrenKatsayisi = random(20, 30);
      } else {
        solFrenKatsayisi = random(15, 25);
      }
      sagFrenKatsayisi = solFrenKatsayisi + random(-1, 2);
    }

    // Donanım veya yazılım kilitlenmelerini önlemek adına fren katsayısı limitleri
    if (sagFrenKatsayisi < 3) sagFrenKatsayisi = 3;
    if (sagFrenKatsayisi > 30) sagFrenKatsayisi = 30; 
    if (solFrenKatsayisi < 3) solFrenKatsayisi = 3;
    if (solFrenKatsayisi > 30) solFrenKatsayisi = 30;

    gozHedefleriniGuncelle();

    // Büyük göz hareketleri sırasında %70 şansla göz kırpma tetiklenerek geçiş gizlenir
    if (random(100) < gozKirparakHareketSans) {
      gozKirpTetikle();
    }
  }

  // -----------------------------------------------------------------------
  // ADIM 6: HAREKETTEN BAĞIMSIZ DUYGU DEĞİŞİM ZAMANLAYICISI
  // -----------------------------------------------------------------------
  if (millis() - sonDuyguZamani > beklemeSuresiDuygu) {
    sonDuyguZamani = millis();
    beklemeSuresiDuygu = random(3000, 8000); 

    byte yeniDuygu = duyguSec();
    if (yeniDuygu != mevcutDuygu) {
      mevcutDuygu = yeniDuygu;

      // Yeni duyguya göre olasılık etkileri ve göz kırpma sıklıkları güncellenir
      if (mevcutDuygu == 0) { // NORMAL
        kismaEtkisi = -500; 
        saskinEtkisi = -400;
        kizginEtkisi = -450;

        minKirpmaSuresi = 2000;
        maxKirpmaSuresi = 5000;
        gozKirparakHareketSans = 70; 
        Serial.println(F("NORMAL"));
      } 
      else if (mevcutDuygu == 1) { // KISMA
        kismaEtkisi = 400;  
        saskinEtkisi = -300; 
        kizginEtkisi = 0;    

        minKirpmaSuresi = 2000;       
        maxKirpmaSuresi = 5000;
        gozKirparakHareketSans = 70;  
        Serial.println(F("KISMA"));
      } 
      else if (mevcutDuygu == 2) { // ŞAŞKIN
        saskinEtkisi = 300;
        kismaEtkisi = -400;  
        kizginEtkisi = 0;    

        minKirpmaSuresi = 6000;      
        maxKirpmaSuresi = 12000;
        gozKirparakHareketSans = 20;  
        Serial.println(F("SASKIN"));
      } 
      else if (mevcutDuygu == 3) { // KIZGIN
        kizginEtkisi = 350;
        saskinEtkisi = -250; 
        kismaEtkisi = 0;

        minKirpmaSuresi = 6000;      
        maxKirpmaSuresi = 12000;
        gozKirparakHareketSans = 70;  
        Serial.println(F("KIZGIN"));
      }

      // Duygu geçiş söndürme çarpanı (Ağırlıklar zamanla azalır)
      kismaEtkisi *= 0.8f;
      saskinEtkisi *= 0.8f;
      kizginEtkisi *= 0.8f;

      // Duygu değişirken geçiş yumuşak/süzülerek olsun diye fren hızlarını yavaşlatıyoruz
      solFrenKatsayisi = random(10, 15);
      sagFrenKatsayisi = solFrenKatsayisi;

      gozHedefleriniGuncelle();
    }
  }

  // -----------------------------------------------------------------------
  // ADIM 7: EKRANA ÇİZİM YAPMA VE YAY (B) / PERSPEKTİF ESNEME HESAPLARI
  // -----------------------------------------------------------------------
  int w1 = (int)solGoz.yatay;
  int h1 = (int)solGoz.dikey;
  int w2 = (int)sagGoz.yatay;
  int h2 = (int)sagGoz.dikey;

  // Perspektif Esneme: Göz kırpmıyorken, bakış yönüne göre derinlik katmak amacıyla esnetme uygulanır.
  if (kirpmaDurumu == 0) { 
    if (ortakhedefX > 68) {      // Sağa Bakış: Sağ göz büyür, sol göz küçülür
      h2 += 1;
      w2 += 1;
      h1 -= 1;
      w1 -= 1;
    } 
    else if (ortakhedefX < 60) { // Sola Bakış: Sol göz büyür, sağ göz küçülür
      h1 += 1;
      w1 += 1;
      h2 -= 1;
      w2 -= 1;
    }
  }

  // Emniyetli boyut kontrolleri (Gözlerin tamamen yok olmaması için en az 4x4 piksel olmalı)
  if (w1 < 4) w1 = 4;
  if (h1 < 4) h1 = 4;
  if (w2 < 4) w2 = 4;
  if (h2 < 4) h2 = 4;

  // Kavisli Yay Çizimi (B Özelliği): Göz hedefe doğru giderken dikeyde kavis yapar.
  float solYayY = 0;
  float sagYayY = 0;
  if (!hedefeVardi) {
    solYayY = -abs(solHedefX - solGoz.x) * 0.08f;
    sagYayY = -abs(sagHedefX - sagGoz.x) * 0.08f;
    solYayY = constrain(solYayY, -4.0f, 0.0f); // Maksimum 4 piksel yukarı doğru kavis
    sagYayY = constrain(sagYayY, -4.0f, 0.0f);
  }

  // Koordinatları ekran sınırlarına göre koruma altına alıyoruz
  int x1 = constrain((int)(solGoz.x - w1 / 2), 0, 127 - w1);
  int y1 = constrain((int)(solGoz.y + solYayY - h1 / 2), 0,  63 - h1);

  int x2 = constrain((int)(sagGoz.x - w2 / 2), 0, 127 - w2);
  int y2 = constrain((int)(sagGoz.y + sagYayY - h2 / 2), 0,  63 - h2);

  // Sol Göz Bebeğini Çiz
  if (h1 > 2 && w1 > 2) {
    display.fillRoundRect(
      x1, y1, w1, h1,
      min(gozKoseYumusatma, min(w1 / 2, h1 / 2)),
      SSD1306_WHITE
    );
  }

  // Sağ Göz Bebeğini Çiz
  if (h2 > 2 && w2 > 2) {
    display.fillRoundRect(
      x2, y2, w2, h2,
      min(gozKoseYumusatma, min(w2 / 2, h2 / 2)),
      SSD1306_WHITE
    );
  }

  // Kaş Maskelerinin Çizimi (Kızgınlık İfadesi İçin Üstten Kesme)
  if (solGoz.kasInme > 1) { 
    display.fillTriangle(
      x1 - 2, y1 - 2,                                      
      x1 + w1 + 2, y1 - 2,                  
      x1 + w1 + 2, y1 + (int)solGoz.kasInme,
      SSD1306_BLACK
    );
  }

  if (sagGoz.kasInme > 1) {
    display.fillTriangle(
      x2 - 2, y2 - 2,                                      
      x2 + w2 + 2, y2 - 2,                  
      x2 - 2, y2 + (int)sagGoz.kasInme,                    
      SSD1306_BLACK
    );
  }

  display.display(); // Hesaplanan tüm frame'i ekrana bas
}
