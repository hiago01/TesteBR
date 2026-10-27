package org.json;
import java.util.*;
/** Stub de teste: parser JSON estrito com os mesmos tipos de retorno do org.json do Android. */
public class JSONObject {
  public static final Object NULL = new Object(){ public String toString(){return "null";} };
  final Map<String,Object> m = new LinkedHashMap<>();
  public JSONObject(){}
  public JSONObject(String s) throws JSONException {
    P p = new P(s); Object o = p.val(); p.ws();
    if (p.i != s.length() || !(o instanceof JSONObject)) throw new JSONException("not an object");
    m.putAll(((JSONObject)o).m);
  }
  public boolean has(String k){return m.containsKey(k);}
  public boolean isNull(String k){return !m.containsKey(k)||m.get(k)==NULL;}
  public Object opt(String k){return m.get(k);}
  public JSONArray optJSONArray(String k){Object o=m.get(k);return o instanceof JSONArray?(JSONArray)o:null;}
  public boolean optBoolean(String k, boolean d){Object o=m.get(k); if(o instanceof Boolean)return (Boolean)o; if(o instanceof String){String s=(String)o; if(s.equalsIgnoreCase("true"))return true; if(s.equalsIgnoreCase("false"))return false;} return d;}
  static class P { String s; int i=0; P(String s){this.s=s;}
    void ws(){while(i<s.length()&&Character.isWhitespace(s.charAt(i)))i++;}
    Object val() throws JSONException { ws(); if(i>=s.length())throw new JSONException("eof"); char c=s.charAt(i);
      if(c=='{'){i++;JSONObject o=new JSONObject();ws(); if(s.charAt(i)=='}'){i++;return o;}
        while(true){ws(); if(s.charAt(i)!='"')throw new JSONException("key"); String k=str(); ws(); if(s.charAt(i++)!=':')throw new JSONException(":"); o.m.put(k,val()); ws(); char d=s.charAt(i++); if(d=='}')return o; if(d!=',')throw new JSONException(",");}}
      if(c=='['){i++;JSONArray a=new JSONArray();ws(); if(s.charAt(i)==']'){i++;return a;}
        while(true){a.l.add(val()); ws(); char d=s.charAt(i++); if(d==']')return a; if(d!=',')throw new JSONException(",");}}
      if(c=='"')return str();
      if(s.startsWith("true",i)){i+=4;return Boolean.TRUE;} if(s.startsWith("false",i)){i+=5;return Boolean.FALSE;} if(s.startsWith("null",i)){i+=4;return NULL;}
      int st=i; while(i<s.length()&&"+-0123456789.eE".indexOf(s.charAt(i))>=0)i++; if(st==i)throw new JSONException("bad token");
      String n=s.substring(st,i); try{ if(n.contains(".")||n.contains("e")||n.contains("E"))return Double.valueOf(n); long v=Long.parseLong(n); return (v>=Integer.MIN_VALUE&&v<=Integer.MAX_VALUE)?(Object)Integer.valueOf((int)v):(Object)Long.valueOf(v);}catch(NumberFormatException e){throw new JSONException("num");}
    }
    String str() throws JSONException { StringBuilder b=new StringBuilder(); i++; while(true){ if(i>=s.length())throw new JSONException("str"); char c=s.charAt(i++); if(c=='"')return b.toString(); if(c=='\\'){char e=s.charAt(i++); b.append(e=='n'?'\n':e);} else b.append(c);} }
  }
}
