/*
 * Copyright (c) 2015 Cesanta Software Limited
 * All rights reserved
 */

#include "internal.h"
#include <sys/time.h>
#include <time.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <stddef.h>

#ifdef __APPLE__
int64_t strtoll(const char *, char **, int);
#endif

typedef double etime_t; /* double is suitable type for ECMA time */
/* inernally we have to use 64-bit integer for some operations */
typedef int64_t etimeint_t;
#define INVALID_TIME NAN

/*++++++  ECMA date & time helpers ++++++*/

#define msPerDay 86400000
#define HoursPerDay 24
#define MinutesPerHour 60
#define SecondsPerMinute 60
#define msPerSecond 1000
#define msPerMinute 60000
#define msPerHour 3600000
#define MonthInYear 12

static etimeint_t ecma_Day(etime_t t) {
  return floor(t/ msPerDay);
}

/* Leap year formula copied from ECMA 5.1 standart as is */
static int ecma_DaysInYear(int y) {
  if(y % 4 != 0 ) {
    return 365;
  } else if( y % 4 == 0 && y % 100 != 0 ) {
    return 366;
  } else if(y % 100 == 0 && y % 400 != 0) {
    return 365;
  } else if(y % 400 == 0) {
    return 366;
  } else {
    return 365;
  }
}

static etimeint_t ecma_DayFromYear(etimeint_t y) {
  return 365 * (y-1970) + floor((y-1969)/4) - floor((y-1901)/100)
                                              + floor((y-1601)/400);
}

static etimeint_t ecma_TimeFromYear(etimeint_t y) {
  return msPerDay * ecma_DayFromYear(y);
}

static int ecma_YearFromTime_s(etime_t t)
{
  int first = floor((t / msPerDay) / 366) + 1970,
              last = floor((t / msPerDay) / 365) + 1970, middle = 0;
  
  if (last < first) {
    int temp = first;
    first = last;
    last = temp;
  }
  
  while (last > first) {
    middle = (last + first) / 2;
    if (ecma_TimeFromYear(middle) > t) {
      last = middle - 1;
    } else {
      if (ecma_TimeFromYear(middle) <= t) {
        if (ecma_TimeFromYear(middle + 1) > t) {
          first = middle;
          break;
        }
        first = middle + 1;
      }
    }
  }
  
  return first;
}

static int ecma_InLeapYear(etime_t t, int year) {
  (void)t;
  return ecma_DaysInYear(year) == 366;
}

static int ecma_DayWithinYear(etime_t t, int year) {
  return (int)(ecma_Day(t) - ecma_DayFromYear(year));
}

static void ecma_getfirstdays(int* days, int isleap) {
  unsigned int i;
  static int sdays[] = {0, 31, 59, 90, 120, 151, 181, 212,
                        243, 273, 304, 334, 365};
  memcpy(days, sdays, sizeof(sdays));
  
  if(isleap) {
    for(i = 2; i < ARRAY_SIZE(sdays); i++) {
      days[i] += 1;
    }
  }
}

static int ecma_MonthFromTime(etime_t t, int year) {
  int days[MonthInYear+1];
  etimeint_t dwy = ecma_DayWithinYear(t, year);
  int ily = ecma_InLeapYear(t, year);
  
  unsigned int i; int ret = -1;
  
  ecma_getfirstdays(days, ily);
  
  for(i = 0; i < ARRAY_SIZE(days) - 1; i++) {
    if(dwy >= days[i] && dwy < days[i+1]) {
      ret = i;
      break;
    }
  }
  
  return ret;
}

static int ecma_DateFromTime(etime_t t, int year) {
  int days[MonthInYear+1];
  int mft = ecma_MonthFromTime(t, year);
  int dwy = ecma_DayWithinYear(t, year);
  int ily = ecma_InLeapYear(t, year);
  
  if(mft > 11) {
    return -1;
  }
  
  ecma_getfirstdays(days, ily);
  
  return dwy - days[mft] + 1;
}

static int ecma_WeekDay(etime_t t) {
  return (ecma_Day(t)+4) % 7;
}

static int ecma_DaylightSavingTA(etime_t t) {
  time_t time = t / 1000;
  struct tm tm;
  memset(&tm, 0, sizeof(t));
  localtime_r(&time, &tm);
  if(tm.tm_isdst > 0) {
    return msPerHour;
  } else {
    return 0;
  }
}

static int ecma_LocalTZA() {
  return (int)-timezone * 1000;
}

static etimeint_t ecma_LocalTime(etime_t t) {
  return t + ecma_LocalTZA() + ecma_DaylightSavingTA(t);
}

static etimeint_t ecma_UTC(etime_t t) {
  return t - ecma_LocalTZA() - ecma_DaylightSavingTA(t-ecma_LocalTZA());
}

static int ecma_HourFromTime(etime_t t) {
  return (etimeint_t)floor(t / msPerHour) % HoursPerDay;
}

static int ecma_MinFromTime(etime_t t) {
  return (etimeint_t)floor(t / msPerMinute) % MinutesPerHour;
}

static int ecma_SecFromTime(etime_t t) {
  return (etimeint_t)floor(t / msPerSecond) % SecondsPerMinute;
}

static int ecma_msFromTime(etime_t t) {
  return (etimeint_t)t % msPerSecond;
}

static etimeint_t ecma_MakeTime(etimeint_t hour, etimeint_t min,
                                    etimeint_t sec, etimeint_t ms) {
  return ((hour * MinutesPerHour + min)
                      * SecondsPerMinute + sec) * msPerSecond + ms;
}

static etimeint_t ecma_MakeDay(int year, int month, int date)
{
  int days[MonthInYear+1];

  etimeint_t yday;
  etimeint_t mday;
  
  year += floor(month / 12);
  
  month = month % 12;
  
  yday = floor(ecma_TimeFromYear(year) / msPerDay);

  ecma_getfirstdays(days, (ecma_DaysInYear(year) == 366));

  mday = days[month];
  
  return yday + mday + date - 1;
}

static etimeint_t ecma_MakeDate(etimeint_t day, etimeint_t time) {
  return (day * msPerDay + time);
}

/*++++ ECMA alternatives to struct tm ++++*/

struct timeparts {
  int year; /* can be negative, up to +-282000 */
  int month; /* 0-11 */
  int day; /* 1-31 */
  int hour; /* 0-23 */
  int min; /* 0-59 */
  int sec; /* 0-59 */
  int msec; 
  int dayofweek; /* 0-6 */
};

static int d_istimeinvalid(etime_t* time) {
  return isnan(*time);
}

/*+++ this functions is used to get current date/time & timezone */

static void d_gettime(etime_t* time) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  *time = (etime_t)tv.tv_sec * 1000 + (etime_t)tv.tv_usec / 1000;
}

static int d_gettimezone() {
  return (int)timezone/60; /* TODO(alashkin): check non-OSX */
}

static char* d_gettzname() {
  return tzname[0]; /* TODO(alashkin): check non-OSX */
}

static etime_t d_mktime_impl(struct timeparts* tp) {
  return ecma_MakeDate(ecma_MakeDay(tp->year, tp->month, tp->day),
                       ecma_MakeTime(tp->hour, tp->min, tp->sec, tp->msec));
}

/*++++ libc mktime alternatives ++++*/

static etime_t d_lmktime(struct timeparts* tp) {
  return ecma_UTC(d_mktime_impl(tp));
}

static etime_t d_gmktime(struct timeparts* tp) {
  return d_mktime_impl(tp);
}

typedef etime_t (*fmaketime)(struct timeparts* );

/*++++ libc gmtime & localtime alternatives ++++*/
static void d_gmtime(etime_t* t, struct timeparts* tp) {
  tp->year = ecma_YearFromTime_s(*t);
  tp->month = ecma_MonthFromTime(*t, tp->year);
  tp->day = ecma_DateFromTime(*t, tp->year);
  tp->hour = ecma_HourFromTime(*t);
  tp->min = ecma_MinFromTime(*t);
  tp->sec = ecma_SecFromTime(*t);
  tp->msec = ecma_msFromTime(*t);
  tp->dayofweek = ecma_WeekDay(*t);
}

static void d_localtime(etime_t* time, struct timeparts* tp) {
  etime_t local_time = ecma_LocalTime(*time);
  d_gmtime(&local_time, tp);
}

typedef void (*fbreaktime)(etime_t* , struct timeparts* );

/*++++ some isXXX helpers ++++*/

static int d_isnumberNAN(struct v7 *v7, val_t obj) {
  return (i_value_of(v7, obj) == V7_TAG_NAN);
}

static void d_isobjvalidforstirng(struct v7* v7, val_t obj) {
  if(!v7_is_object(obj) || (v7_is_object(obj)
                    && i_value_of(v7, obj) == V7_TAG_NAN)) {
    throw_exception(v7, "TypeError", "Date is invalid (for string)");
  }
}

static int d_iscalledasfunction(struct v7 *v7, val_t this_obj) {
  /* TODO(alashkin): verify this statement */
  return is_prototype_of(this_obj, v7->date_prototype);
}

/*++++ from/to string helpers ++++*/

static int d_timetoISOstr(etime_t* time, char* buf, size_t buf_size) {
  /* ISO format: "+XXYYYY-MM-DDTHH:mm:ss.sssZ"; */
  struct timeparts tp;
  char use_ext = 0;
  const char* ey_frm="%06d-%02d-%02dT%02d:%02d:%02d.%03dZ";
  const char* simpl_frm="%04d-%02d-%02dT%02d:%02d:%02d.%03dZ";
  
  (void)buf_size;
  
  d_gmtime(time, &tp);

  if(abs(tp.year) > 9999 || tp.year < 0) {
    *buf = tp.year>0? '+':'-';
    use_ext = 1;
  }
  
  return snprintf(buf + use_ext, buf_size - use_ext, 
        use_ext? ey_frm: simpl_frm, tp.year, tp.month + 1,
                    tp.day, tp.hour, tp.min, tp.sec, tp.msec) + use_ext;
}

/* non-locale function should always return in english and 24h-format */
static const char* wday_name[] = {
  "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

static const char* mon_name[] = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun",
  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

int d_getnumbyname(const char** arr, int arr_size, const char *str) {
  int i;
  for(i = 0; i < arr_size; i++) {
    if(strncmp(arr[i], str, 3) == 0 ) {
      return i + 1;
    }
  }
  
  return -1;
}

int date_parse(const char* str, int* a1, int* a2, int* a3, char
                                                    sep, char* rest) {
  char frmDate[] = " %d/%d/%d%[^\0]";
  frmDate[3] = frmDate[6] = sep;
  return sscanf(str, frmDate, a1, a2, a3, rest);
}

#define NO_TZ 0x7FFFFFFF

/* not very smart but simple, and working according 
                                    to ECMA5.1 StringToDate function */
static int d_parsedatestr(const char* str, struct timeparts* tp, int* tz) {
  char gmt[4];
  char buf1[100] = {0}, buf2[100] = {0};
  int res = 0;
  memset(tp, 0, sizeof(*tp));
  *tz = NO_TZ;
  
  /* #1: trying toISOSrting() format */
  {
    const char* frmISOString = " %d-%02d-%02dT%02d:%02d:%02d.%03dZ";
    res = sscanf(str, frmISOString, &tp->year, &tp->month, &tp->day,
                                &tp->hour, &tp->min, &tp->sec, &tp->msec);
    if(res == 7) {
      *tz = 0;
      return 1;
    }
  }
  
  /* #2: trying getdate() - it never works on some OS, but... */
  {
    struct tm* tm = getdate(str);
    if(tm != NULL) {
      tp->year = tm->tm_year + 1900;
      tp->month = tm->tm_mon;
      tp->day = tm->tm_mday;
      tp->hour = tm->tm_hour;
      tp->min = tm->tm_min;
      tp->sec = tm->tm_sec;
    
      return 1;
    }
  }
  
  /* #3: trying toString()/toUTCString()/toDateFormat() formats */
  {
    char month[4];
    const char* frmString = " %03*s %03s %02d %d %02d:%02d:%02d %03s%d";
    res = sscanf(str, frmString, month, &tp->day, &tp->year, &tp->hour,
                                            &tp->min, &tp->sec, gmt, tz);
    if(res == 3 || (res >= 6 && res <= 8) ) {
      if( (tp->month = d_getnumbyname(mon_name,
                                      ARRAY_SIZE(mon_name), month)) != -1) {
        if(res == 7 && strncmp(gmt, "GMT", 3) == 0 ) {
          *tz = 0;
        }
        return 1;
      }
    }
  }
  
  /* #4: trying the rest */
  
  /* trying date */

  if(!(date_parse(str, &tp->month, &tp->day, &tp->year, '/', buf1) >= 3 ||
       date_parse(str, &tp->day, &tp->month, &tp->year, '.', buf1) >= 3 ||
       date_parse(str, &tp->year, &tp->month, &tp->day, '-', buf1) >= 3 ) ) {
    return 0;
  }
  
  /*  there is date, trying time; from here return 0 only on errors */
  
  /* trying HH:mm */
  {
    const char* frmMMhh = " %d:%d%[^\0]";
    res = sscanf(buf1, frmMMhh, &tp->hour, &tp->min, buf2);
    /* can't get time, but have some symbols, assuming error */
    if(res < 2 ) {
      return (strlen(buf2) == 0);
    }
  }
  
  /* trying seconds */
  {
    const char* frmss = ":%d%[^\0]";
    memset(buf1, 0, sizeof(buf1));
    res = sscanf(buf2, frmss, &tp->sec, buf1);
  }
  
  /* even if we don't get seconds we gonna try to get tz */
  {
    char* rest = res? buf1: buf2;
    char* buf = res? buf2: buf1;
    const char* frmtz = " %03s%d%[^\0]";
    
    res = sscanf(rest, frmtz, gmt, tz, buf);
    if(res == 1 && strncmp(gmt, "GMT", 3) == 0) {
      *tz = 0;
    }
  }

  /* return OK if we are at the end of string */
  return (res <= 2);
}

static int d_timeFromString(etime_t* time, const char* str, size_t buf_size) {
  struct timeparts tp;
  int tz;
  
  *time = INVALID_TIME;
  
  if(buf_size > 100) {
    /* too long for valid date string */
    return 0;
  }
  
  if(d_parsedatestr(str, &tp, &tz)) {
    /* check results */
    int valid = 0;
    
    tp.month--;
    valid = tp.day >=1 && tp.day <= 31;
    valid &= tp.month >=0 && tp.month <= 11;
    valid &= tp.hour >= 0 && tp.hour <= 23;
    valid &= tp.min >= 0 && tp.min <= 59;
    valid &= tp.sec >= 0 && tp.sec <= 59;
    
    if(tz != NO_TZ && tz > 12) {
      tz /= 100;
    }
    
    valid &= (abs(tz) <= 12 || tz == NO_TZ);
    
    if(valid) {
      *time = d_gmktime(&tp);
      
      tz = (tz == NO_TZ)? -d_gettimezone() * msPerMinute : tz * msPerHour;
      *time -= tz;
    }
  }

  return *time != INVALID_TIME;
}

typedef int (*ftostring)(struct timeparts*, char*, int);

static val_t d_tostring(struct v7 *v7, val_t obj, fbreaktime breatimefunc,
                                        ftostring tostringfunc, int addtz) {
  struct timeparts tp;
  int len;
  char buf[100];
  etime_t time;
  
  d_isobjvalidforstirng(v7, obj);
  
  time = i_as_num(v7, obj);
  
  breatimefunc(&time, &tp);
  len = tostringfunc(&tp, buf, addtz);
  
  return v7_create_string(v7, buf, len, 1);
}

struct d_locale {
  char locale[50];
};

static void d_getcurrentlocale(struct d_locale* loc) {
  strcpy(setlocale(LC_TIME, 0), loc->locale);
}

static void d_setlocale(struct d_locale* loc) {
  setlocale(LC_TIME, loc? loc->locale: "");
}

static val_t d_tolocalestr(struct v7 *v7, val_t obj, const char* frm) {
  char buf[50];
  size_t len;
  struct tm t;
  etime_t time;
  struct d_locale prev_locale;
  time_t tsecs;
  
  d_isobjvalidforstirng(v7, obj);
  
  time = i_as_num(v7, obj);
  
  d_getcurrentlocale(&prev_locale);
  d_setlocale(0);
  
  tsecs = time / 1000;
  localtime_r(&tsecs, &t);
  
  len = strftime(buf, sizeof(buf), frm, &t);
  
  d_setlocale(&prev_locale);
  
  return v7_create_string(v7, buf, len, 1);
}

static int d_tptodatestr(struct timeparts* tp, char* buf, int addtz) {
  int use_ext;

  const char* ey_frm = "%s %s %02d %06d";
  const char* simpl_frm ="%s %s %02d %04d";
  
  (void)addtz;
  
  use_ext = (abs(tp->year) > 9999 || tp->year < 0);
  
  return sprintf(buf, use_ext? ey_frm: simpl_frm, wday_name[tp->dayofweek],
                                    mon_name[tp->month], tp->day, tp->year);
}

static int d_tptotimestr(struct timeparts* tp, char* buf, int addtz) {
  int len;

  len = sprintf(buf, "%02d:%02d:%02d GMT", tp->hour, tp->min, tp->sec);
  
  if(addtz && d_gettimezone() != 0) {
    len = sprintf(buf+len, "%c%02d00 (%s)", d_gettimezone() > 0? '-':'+',
                                    abs(d_gettimezone()/60), d_gettzname());
  }
  
  return (int)strlen(buf);
}

static int d_tptostr(struct timeparts* tp, char* buf, int addtz) {
  int len = d_tptodatestr(tp, buf, addtz);
  *(buf + len) = ' ';
  return d_tptotimestr(tp, buf + len + 1, addtz) + len + 1;
}

/* +++ converting arg to string ++++*/
/* according ECMA 5.1 we should try to convert any type of 
 * parameter to number before goint NaN 
*/

static int d_argtoint(struct v7* v7, val_t* obj, etime_t* ret) {
  *ret = INVALID_TIME;
  if(v7_is_double(*obj)) {
    *ret = trunc(v7_to_double(*obj));
    if(isinf(*ret)) {
      /* in case of inf we should use NaN */
      *ret = INVALID_TIME;
    }
  } else if(v7_is_boolean(*obj)) {
    /* just 0 or 1 */
    *ret = v7_to_boolean(*obj);
  } else if(v7_is_string(*obj)) {
    size_t size;
    const char* str = v7_to_string(v7, obj, &size);
    char* endptr;
    *ret = strtoll(str, &endptr, 10);
    if(endptr != str + size) {
      *ret = INVALID_TIME;
    }
  } else if(v7_is_object(*obj)) {
    /* object - going under */
    val_t val = i_value_of(v7, *obj);
    return d_argtoint(v7, &val, ret);
  }
  
  return *ret != INVALID_TIME;
}

/*+++ setXXXX & getXXXX helpers ++++*/
static struct timeparts* d_getTP(struct v7 *v7, val_t this_obj, val_t args,
                            struct timeparts* tp, fbreaktime breaktimefunc) {
  etime_t time;
  (void)args;
  time = v7_to_double(i_value_of(v7, this_obj));
  breaktimefunc(&time, tp);
  return tp;
}

struct dtimepartsarr {
  etime_t args[7];
};

enum detimepartsarr { tpyear = 0, tpmonth , tpdate , tphours, tpminutes,
                                                  tpseconds, tpmsec, tpmax };

static etime_t d_changepartoftime(etime_t* current, struct dtimepartsarr* a,
                            fbreaktime breaktimefunc, fmaketime maketimefunc) {
  /* 0 = year, 1 = month , 2 = date , 3 = hours, 
                4 = minutes, 5 = seconds, 6 = ms */
  struct timeparts tp;
  unsigned long i;
  
  int* tp_arr[7]; /* C89 doesn't handle initialization 
                          like x = {&tp.year, &tp.month, .... } */
  tp_arr[0] = &tp.year; tp_arr[1] = &tp.month; tp_arr[2] = &tp.day;
  tp_arr[3] = &tp.hour; tp_arr[4] = &tp.min; tp_arr[5] = &tp.sec;
  tp_arr[6] = &tp.msec;
  
  memset(&tp, 0, sizeof(tp));
  
  if(breaktimefunc != NULL) {
    breaktimefunc(current, &tp);
  }
  
  for(i = 0; i < ARRAY_SIZE(tp_arr); i++) {
    if(!d_istimeinvalid(&a->args[i])) {
      *tp_arr[i] = (int)a->args[i];
    }
  }
  
  return maketimefunc(&tp);
}

static etime_t d_time_number_from_tp(struct v7 *v7, val_t this_obj, val_t args,
            int start_pos, fbreaktime breaktimefunc, fmaketime makefilefunc) {
  etime_t ret_time = INVALID_TIME;
  long cargs;
  
  if((cargs=v7_array_length(v7, args)) >= 1 && !d_isnumberNAN(v7, this_obj)) {
    int i;
    struct dtimepartsarr a = {{INVALID_TIME, INVALID_TIME, INVALID_TIME,
                    INVALID_TIME, INVALID_TIME, INVALID_TIME, INVALID_TIME}};
    etime_t new_part = INVALID_TIME;
    
    for(i=0; i < cargs && (i+start_pos < tpmax); i++) {
      val_t argi = v7_array_at(v7, args, i);
      if(!d_argtoint(v7, &argi, &new_part)) {
        break;
      }
      
      a.args[i+start_pos] = new_part;
    }
    
    if(!d_istimeinvalid(&new_part)) {
      etime_t current_time = v7_to_double(i_value_of(v7, this_obj));
      ret_time = d_changepartoftime(&current_time, &a,
                                          breaktimefunc, makefilefunc);
    }
  }
  
  return ret_time;
}

static val_t d_setTimePart(struct v7 *v7, val_t this_obj, val_t args,
            int start_pos, fbreaktime breaktimefunc, fmaketime makefilefunc) {
  val_t n;
  etime_t ret_time = d_time_number_from_tp(v7, this_obj, args, start_pos,
                                                breaktimefunc, makefilefunc);
                                                                       
  n = v7_create_number(ret_time);
  v7_set_property(v7, this_obj, "", 0, V7_PROPERTY_HIDDEN, n);
                                                                       
  return n;
}

/*++++++++++++++ API +++++++++++++++*/

/* constructor */
static val_t Date_ctor(struct v7 *v7, val_t this_obj, val_t args) {
  etime_t ret_time = 0;   
  if(v7_is_object(this_obj) && this_obj != v7->global_object) {
    long cargs = v7_array_length(v7, args);
    if(cargs <=0 ) {
      /* no parameters - return current date & time */
      d_gettime(&ret_time);
    } else if(cargs == 1) {
      /* one parameter */
      val_t arg = v7_array_at(v7, args, 0);
      if(v7_is_string(arg)){ /* it could be string */
        size_t str_size;
        const char* str = v7_to_string(v7, &arg, &str_size);
        d_timeFromString(&ret_time, str, str_size);
      } else {
        d_argtoint(v7, &arg, &ret_time);
      }
    } else {
      /* 2+ paramaters - should be parts of a date */
      struct dtimepartsarr a;
      int i;
      
      memset(&a, 0, sizeof(a));
      
      for(i=0; i < cargs; i++) {
        val_t val = v7_array_at(v7, args, i);
        if(!d_argtoint(v7, &val, &a.args[i])) {
          break;
        }
      }
      
      if(i>=cargs) {
        if(a.args[tpdate] == 0 ) {
          a.args[tpdate] = 1; /* If date is supplied then let 
                               dt be ToNumber(date); else let dt be 1. */
        }
      
        if(a.args[tpyear] >=0 && a.args[tpyear] <=99) {
          a.args[tpyear] += 1900;
          /* If y is not NaN and 0 <= ToInteger(y) <= 99,
                  then let yr be 1900+ToInteger(y); otherwise, let yr be y. */
        }

        ret_time = ecma_UTC(d_changepartoftime(0, &a, 0, d_gmktime));
      }
    }
  
    v7_to_object(this_obj)->prototype = v7_to_object(v7->date_prototype);
    v7_set_property(v7, this_obj, "", 0, V7_PROPERTY_HIDDEN,
                                        v7_create_number(ret_time));
    return this_obj;
  } else {
    /* according to 15.9.2.1 we should ignore all 
                          parameters in case of function-call */
    struct timeparts tp;
    char buf[50];
    int len;
    
    d_gettime(&ret_time);
    d_localtime(&ret_time, &tp);
    len = d_tptostr(&tp, buf, 1);
    
    return v7_create_string(v7, buf, len, 1);
  }
}

/*++++ toXXXString functions ++++*/
static val_t Date_toISOString(struct v7 *v7, val_t this_obj, val_t args) {
  char buf[30];
  etime_t time; int len;
  (void)args;
  
  d_isobjvalidforstirng(v7, this_obj);
  
  time = v7_to_double(i_value_of(v7, this_obj));
  len = d_timetoISOstr(&time, buf, sizeof(buf));
  
  return v7_create_string(v7, buf, len, 1);
}

/* using macros to avoid copy-paste technic */
#define DEF_TOSTR(funcname, breaktimefunc,tostrfunc, addtz) \
  static val_t Date_to##funcname(struct v7 *v7, val_t this_obj, val_t args) {  \
    (void)args; \
    return d_tostring(v7, this_obj, breaktimefunc, tostrfunc, addtz); \
  }

DEF_TOSTR(UTCString, d_gmtime, d_tptostr, 0)
DEF_TOSTR(String, d_localtime, d_tptostr, 1)
DEF_TOSTR(DateString, d_localtime, d_tptodatestr, 1)
DEF_TOSTR(TimeString, d_localtime, d_tptotimestr, 1)


#define DEF_TOLOCALESTR(funcname, frm) \
  static val_t Date_to##funcname(struct v7 *v7, val_t this_obj, val_t args) { \
    (void)args; \
    return d_tolocalestr(v7, this_obj, frm);  \
  }

DEF_TOLOCALESTR(LocaleString, "%c")
DEF_TOLOCALESTR(LocaleDateString, "%x")
DEF_TOLOCALESTR(LocaleTimeString, "%X")

/*++++ get functions ++++*/

#define DEF_GET_TP_FUNC(funcName, tpmember, breaktimefunc) \
  static val_t Date_get##funcName(struct v7 *v7, val_t this_obj, val_t args) { \
    struct timeparts tp; \
    return v7_create_number(d_isnumberNAN(v7, this_obj)? NAN: d_getTP(v7, \
            this_obj, args, &tp, breaktimefunc)->tpmember); \
  }

#define DEF_GET_TP(funcName, tpmember) \
  DEF_GET_TP_FUNC(UTC##funcName, tpmember, d_gmtime) \
  DEF_GET_TP_FUNC(funcName, tpmember, d_localtime)


DEF_GET_TP(Date, day)
DEF_GET_TP(FullYear, year)
DEF_GET_TP(Month, month)
DEF_GET_TP(Hours, hour)
DEF_GET_TP(Minutes, min)
DEF_GET_TP(Seconds, sec)
DEF_GET_TP(Milliseconds, msec)
DEF_GET_TP(Day, dayofweek)

/*++++ set functions ++++*/

static val_t Date_setTime(struct v7 *v7, val_t this_obj, val_t args) {
  etime_t ret_time = INVALID_TIME;
  val_t n;
  if(v7_array_length(v7, args) >= 1) {
    val_t arg0 = v7_array_at(v7, args, 0);
    d_argtoint(v7, &arg0, &ret_time);
  }

  n = v7_create_number(ret_time);
  v7_set_property(v7, this_obj, "", 0, V7_PROPERTY_HIDDEN, n);
  return n;
}


#define DEF_SET_TP(name, start_pos) \
  static val_t Date_setUTC##name(struct v7 *v7, val_t this_obj, val_t args) { \
    return d_setTimePart(v7, this_obj, args, start_pos, d_gmtime, d_gmktime); \
  }\
  static val_t Date_set##name(struct v7 *v7, val_t this_obj, val_t args) { \
    return d_setTimePart(v7, this_obj, args, start_pos, \
                                          d_localtime, d_lmktime); \
  }

DEF_SET_TP(Milliseconds, tpmsec)
DEF_SET_TP(Seconds, tpseconds)
DEF_SET_TP(Minutes, tpminutes)
DEF_SET_TP(Hours, tphours)
DEF_SET_TP(Date, tpdate)
DEF_SET_TP(Month, tpmonth)
DEF_SET_TP(FullYear, tpyear)

/*++++ other API ++++*/

static val_t Date_toJSON(struct v7 *v7, val_t this_obj, val_t args) {
  return Date_toISOString(v7, this_obj, args);
}

static val_t Date_valueOf(struct v7 *v7, val_t this_obj, val_t args) {
  (void)args;
  if(!v7_is_object(this_obj) || (v7_is_object(this_obj)
     && v7_to_object(this_obj)->prototype != v7_to_object(v7->date_prototype))){
    throw_exception(v7, "TypeError", "Date.valueOf called on non-Date object");
  }
  
  return Obj_valueOf(v7, this_obj, args);
}

static val_t Date_getTime(struct v7 *v7, val_t this_obj, val_t args) {
  return Date_valueOf(v7, this_obj, args);
}

static val_t Date_getTimezoneOffset(struct v7 *v7, val_t this_obj, val_t args) {
  (void)args; (void)v7; (void)this_obj;
  return v7_create_number(d_gettimezone());
}

static val_t Date_now(struct v7 *v7, val_t this_obj, val_t args) {
  etime_t ret_time;
  (void)args;
  
  if(!d_iscalledasfunction(v7, this_obj)) {
    throw_exception(v7, "TypeError", "Date.now() called on object");
  }

  d_gettime(&ret_time);
  
  return v7_create_number(ret_time);
}

static val_t Date_parse(struct v7 *v7, val_t this_obj, val_t args) {
  etime_t ret_time = INVALID_TIME;
  
  (void)args;
  
  if(!d_iscalledasfunction(v7, this_obj)) {
    throw_exception(v7, "TypeError", "Date.parse() called on object");
  }
  
  if(v7_array_length(v7, args) >= 1) {
    val_t arg0 = v7_array_at(v7, args, 0);
    if(v7_is_string(arg0)) {
      size_t size;
      const char* time_str = v7_to_string(v7, &arg0, &size);
      
      d_timeFromString(&ret_time, time_str, size);
    }
  }
  
  return v7_create_number(ret_time);
}

static val_t Date_UTC(struct v7 *v7, val_t this_obj, val_t args) {
  etime_t ret_time;
  (void)args;
  
  if(!d_iscalledasfunction(v7, this_obj)) {
    throw_exception(v7, "TypeError", "Date.now() called on object");
  }

  ret_time = d_time_number_from_tp(v7, this_obj, args, tpyear, 0, d_gmktime);
  return v7_create_number(ret_time);
}

/****** Initialization *******/

/* we should set V7_PROPERTY_DONT_ENUM for all Date props */
/* TODO(???): check other objects */

static int d_set_cfunc_prop(struct v7 *v7, val_t o, const char *name,
                                                        v7_cfunction_t f) {
  return v7_set_property(v7, o, name, strlen(name),
                         V7_PROPERTY_DONT_ENUM,  v7_create_cfunction(f));
}

#define DECLARE_GET(func) \
  d_set_cfunc_prop(v7, v7->date_prototype, "getUTC"#func, Date_getUTC##func); \
  d_set_cfunc_prop(v7, v7->date_prototype, "get"#func, Date_get##func); \

#define DECLARE_GET_AND_SET(func)   \
  DECLARE_GET(func) \
  d_set_cfunc_prop(v7, v7->date_prototype, "setUTC"#func, Date_setUTC##func); \
  d_set_cfunc_prop(v7, v7->date_prototype, "set"#func, Date_set##func);


V7_PRIVATE void init_date(struct v7 *v7) {
  val_t date = create_object(v7, v7->date_prototype);
  val_t ctor = v7_create_cfunction(Date_ctor);
  unsigned int attrs = V7_PROPERTY_READ_ONLY |
                    V7_PROPERTY_DONT_ENUM | V7_PROPERTY_DONT_DELETE;
  v7_set_property(v7, date, "", 0, V7_PROPERTY_HIDDEN, ctor);
  v7_set_property(v7, date, "prototype", 9, attrs, v7->date_prototype);
  d_set_cfunc_prop(v7, v7->date_prototype, "constructor", Date_ctor);
  v7_set_property(v7, v7->global_object, "Date", 6, V7_PROPERTY_DONT_ENUM, date);
  
  DECLARE_GET_AND_SET(Date);
  DECLARE_GET_AND_SET(FullYear);
  DECLARE_GET_AND_SET(Month);
  DECLARE_GET_AND_SET(Hours);
  DECLARE_GET_AND_SET(Minutes);
  DECLARE_GET_AND_SET(Seconds);
  DECLARE_GET_AND_SET(Milliseconds);
  DECLARE_GET(Day);
 
  d_set_cfunc_prop(v7, v7->date_prototype, "getTimezoneOffset",
                                                    Date_getTimezoneOffset);
 
  d_set_cfunc_prop(v7, v7->date_prototype, "getTime", Date_getTime);
  d_set_cfunc_prop(v7, v7->date_prototype, "toISOString", Date_toISOString);
  d_set_cfunc_prop(v7, v7->date_prototype, "valueOf", Date_valueOf);

  d_set_cfunc_prop(v7, v7->date_prototype, "setTime", Date_setTime);
  d_set_cfunc_prop(v7, v7->date_prototype, "now", Date_now);
  d_set_cfunc_prop(v7, v7->date_prototype, "parse", Date_parse);
  d_set_cfunc_prop(v7, v7->date_prototype, "UTC", Date_UTC);
  d_set_cfunc_prop(v7, v7->date_prototype, "toString", Date_toString);
  d_set_cfunc_prop(v7, v7->date_prototype, "toDateString", Date_toDateString);
  d_set_cfunc_prop(v7, v7->date_prototype, "toTimeString", Date_toTimeString);
  d_set_cfunc_prop(v7, v7->date_prototype, "toUTCString", Date_toUTCString);
  d_set_cfunc_prop(v7, v7->date_prototype, "toLocaleString",
                                                    Date_toLocaleString);
  d_set_cfunc_prop(v7, v7->date_prototype, "toLocaleDateString",
                                                    Date_toLocaleDateString);
  d_set_cfunc_prop(v7, v7->date_prototype, "toLocaleTimeString",
                                                    Date_toLocaleTimeString);
  d_set_cfunc_prop(v7, v7->date_prototype, "toJSON", Date_toJSON);

  tzset();
}
