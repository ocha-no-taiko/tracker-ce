#include "dawControl/dawControlConfig.h"
#include "core/interfaceDefs.h"
#include <cstdio>
#include <cstring>
static int fails = 0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } }while(0)
int main(){
  DawControlConfig c; dawControlConfigSetDefaults(c);
  CHECK(c.controlChannel==16 && c.exitButton==7 && c.scaleLength==7);
  FILE* f=fopen(SDJSON,"rb"); char buf[8192]; size_t n=fread(buf,1,sizeof buf,f); fclose(f);
  DawControlConfig d; dawControlConfigSetDefaults(d);
  CHECK(dawControlConfigParse(buf,n,d));
  CHECK(memcmp(&c,&d,sizeof c)==0); // sample file == defaults
  const char* j=R"({"controlChannel": 3, "noteChannel": 99, "jogCc": -5,
    "noteMode": {"root": 48, "scale": [0,3,7], "ledRoot": 40, "unknown": {"a":[1,2]}},
    "localButtons": {"exit": "Rec", "mode": null, "octaveUp": 31, "octaveDown": "Bogus"},
    "labels": ["A", "Very long label text here", "", "D"], "extra": "x,}]"})";
  DawControlConfig e; dawControlConfigSetDefaults(e);
  CHECK(dawControlConfigParse(j,strlen(j),e));
  CHECK(e.controlChannel==3); CHECK(e.noteChannel==16); CHECK(e.jogCc==0);
  CHECK(e.noteRoot==48); CHECK(e.scaleLength==3 && e.scale[1]==3); CHECK(e.ledRoot==31);
  CHECK(e.exitButton==interfaceButtonRec); CHECK(e.modeButton==DawControlConfig::kNoButton);
  CHECK(e.octaveUpButton==31); CHECK(e.octaveDownButton==interfaceButtonDown);
  CHECK(strcmp(e.labels[0],"A")==0); CHECK(strlen(e.labels[1])==15); CHECK(e.labels[2][0]==0);
  CHECK(strcmp(e.labels[4],"Redo")==0); CHECK(strcmp(e.labels[7],"EXIT")==0);
  const char* sc=R"({"screen": {"title": "T", "credit": "© me", "infoLabels": ["F", 3, "P"],
    "colors": {"background": "#0a0B0c", "text": "bad", "line": "123456"}, "x": [1]},
    "feedback": {"signatureNumeratorCc": 90}})";
  DawControlConfig s; dawControlConfigSetDefaults(s);
  CHECK(dawControlConfigParse(sc,strlen(sc),s));
  CHECK(strcmp(s.title,"T")==0); CHECK(strcmp(s.credit,"\xc2\xa9 me")==0);
  CHECK(strcmp(s.infoLabels[0],"F")==0); CHECK(strcmp(s.infoLabels[1],"Project Name:")==0);
  CHECK(strcmp(s.infoLabels[2],"P")==0); CHECK(strcmp(s.tagline,"Don't push, Track.")==0);
  CHECK(s.backgroundColor==0x0A0B0C); CHECK(s.textColor==0xFFFFFF); CHECK(s.lineColor==0x123456);
  CHECK(s.signatureNumeratorCc==90 && s.signatureDenominatorCc==116);
  const char* bad=R"({"controlChannel": 3, "labels": ["unterminated)";
  DawControlConfig b; dawControlConfigSetDefaults(b);
  CHECK(!dawControlConfigParse(bad,strlen(bad),b));
  const char* bad2=R"({"controlChannel" 3})";
  CHECK(!dawControlConfigParse(bad2,strlen(bad2),b));
  printf(fails? "%d failures\n" : "all parser checks passed\n", fails);
  return fails?1:0;
}
