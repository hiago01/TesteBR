package org.json;
import java.util.*;
public class JSONArray {
  final List<Object> l = new ArrayList<>();
  public int length(){return l.size();}
  public Object opt(int i){return i>=0&&i<l.size()?l.get(i):null;}
  public JSONObject optJSONObject(int i){Object o=opt(i);return o instanceof JSONObject?(JSONObject)o:null;}
}
