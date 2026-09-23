#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <cmath>
#define PROGMEM
#define PSTR(x) x
#define PGM_P const char*
typedef unsigned char byte;
typedef bool boolean;
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT 0
#define INPUT_PULLUP 2
inline uint8_t pgm_read_byte(const void* p){ return *(const uint8_t*)p; }
inline size_t strlen_P(const char* s){ return strlen(s); }
inline void* memcpy_P(void* d, const void* s, size_t n){ return memcpy(d,s,n); }
inline bool isDigit(int c){ return c>='0'&&c<='9'; }
inline bool isAlpha(int c){ return (c>='a'&&c<='z')||(c>='A'&&c<='Z'); }
inline void delay(unsigned long){}
inline void delayMicroseconds(unsigned int){}
inline unsigned long millis(){ return 0; }
inline unsigned long micros(){ return 0; }
inline void pinMode(int,int){}
inline void digitalWrite(int,int){}
inline int digitalRead(int){ return 1; }

class String {
  std::string s;
public:
  String(){}
  String(const char* c):s(c?c:""){}
  String(const std::string& x):s(x){}
  String(int v){ char b[32]; snprintf(b,sizeof b,"%d",v); s=b; }
  String(unsigned v){ char b[32]; snprintf(b,sizeof b,"%u",v); s=b; }
  String(long v){ char b[32]; snprintf(b,sizeof b,"%ld",v); s=b; }
  String(unsigned long v){ char b[32]; snprintf(b,sizeof b,"%lu",v); s=b; }
  String(uint8_t v){ char b[32]; snprintf(b,sizeof b,"%u",(unsigned)v); s=b; }
  String(uint16_t v){ char b[32]; snprintf(b,sizeof b,"%u",(unsigned)v); s=b; }
  String(double v,int d=2){ char b[64]; snprintf(b,sizeof b,"%.*f",d,v); s=b; }
  String(float v,int d=2){ char b[64]; snprintf(b,sizeof b,"%.*f",d,(double)v); s=b; }
  size_t length() const { return s.size(); }
  const char* c_str() const { return s.c_str(); }
  char operator[](size_t i) const { return s[i]; }
  void reserve(size_t n){ s.reserve(n); }
  void trim(){ size_t a=s.find_first_not_of(" \t\r\n"); size_t b=s.find_last_not_of(" \t\r\n");
               s = (a==std::string::npos)?"":s.substr(a,b-a+1); }
  int indexOf(char c) const { size_t p=s.find(c); return p==std::string::npos?-1:(int)p; }
  int indexOf(char c,int from) const { size_t p=s.find(c,from); return p==std::string::npos?-1:(int)p; }
  String substring(int a) const { return String(s.substr(a)); }
  String substring(int a,int b) const { return String(s.substr(a,b-a)); }
  long toInt() const { return atol(s.c_str()); }
  String& operator+=(const String& o){ s+=o.s; return *this; }
  String& operator+=(const char* o){ s+=o; return *this; }
  String& operator+=(char o){ s+=o; return *this; }
  friend String operator+(const String& a,const String& b){ return String(a.s+b.s); }
  friend String operator+(const String& a,const char* b){ return String(a.s+b); }
  friend String operator+(const char* a,const String& b){ return String(std::string(a)+b.s); }
  bool operator==(const char* o) const { return s==o; }
  bool operator!=(const char* o) const { return s!=o; }
};

class SerialClass {
public:
  void begin(unsigned long){}
  void print(const char*){} void print(int){} void print(const String&){}
  void println(){} void println(const char*){} void println(int){} void println(const String&){}
  void println(bool){}
  template<class T> void println(T){}
  void printf(const char*,...){}
};
extern SerialClass Serial;

class IPAddressStub { public: String toString() const { return String("0.0.0.0"); } };
