package test;
import com.samp.mobile.launcher.util.ServerConfigParser;
import com.samp.mobile.launcher.util.ServerConfigParser.*;
public class ParserTest {
  static int pass=0, fail=0;
  static String entry(String ip, String port){ return "{\"number\":1,\"name\":\"Meu Servidor\",\"ip\":"+ip+",\"port\":"+port+",\"online\":0,\"maxplayers\":100,\"password\":false}"; }
  static String wrap(String... e){ return "{\"query\":["+String.join(",",e)+"]}"; }
  static void ok(String name, String json, String exIp, int exPort){
    try { ServerConfig c=ServerConfigParser.parse(json); boolean g=c.ip.equals(exIp)&&c.port==exPort; r(name,g,g?"":("got "+c.ip+":"+c.port)); }
    catch(ConfigException e){ r(name,false,"unexpected error: "+e.getMessage()); } }
  static void bad(String name, String json, String contains){
    try { ServerConfigParser.parse(json); r(name,false,"accepted invalid input"); }
    catch(ConfigException e){ boolean g=e.getMessage().contains(contains); r(name,g,g?"":("msg="+e.getMessage())); } }
  static void r(String n, boolean g, String why){ if(g)pass++; else fail++; System.out.println((g?"PASS ":"FAIL ")+n+(g?"":"  -> "+why)); }
  public static void main(String[] x){
    ok("valid single server (doc example w/ real-looking ip)", wrap(entry("\"203.0.113.10\"","7777")), "203.0.113.10", 7777);
    ok("valid hostname", wrap(entry("\"play.example.com\"","7778")), "play.example.com", 7778);
    ok("valid localhost", wrap(entry("\"localhost\"","7777")), "localhost", 7777);
    ok("port as numeric string", wrap(entry("\"10.0.0.5\"","\"7777\"")), "10.0.0.5", 7777);
    ok("MULTIPLE servers -> only first used", wrap(entry("\"10.0.0.1\"","7777"), entry("\"10.0.0.2\"","8888"), entry("\"10.0.0.3\"","9999")), "10.0.0.1", 7777);
    ok("2nd entry invalid is ignored", wrap(entry("\"10.0.0.1\"","7777"), "{\"garbage\":true}"), "10.0.0.1", 7777);
    ok("optional fields absent", "{\"query\":[{\"name\":\"A\",\"ip\":\"10.0.0.1\",\"port\":7777}]}", "10.0.0.1", 7777);
    bad("placeholder IP_REAL_DO_SERVIDOR rejected", wrap(entry("\"IP_REAL_DO_SERVIDOR\"","7777")), "invalid \"ip\"");
    bad("ip 999.1.1.1 rejected", wrap(entry("\"999.1.1.1\"","7777")), "invalid \"ip\"");
    bad("ip 0.0.0.0 rejected", wrap(entry("\"0.0.0.0\"","7777")), "invalid \"ip\"");
    bad("ip with port rejected", wrap(entry("\"10.0.0.1:7777\"","7777")), "invalid \"ip\"");
    bad("ip with scheme rejected", wrap(entry("\"http://10.0.0.1\"","7777")), "invalid \"ip\"");
    bad("ip with space rejected", wrap(entry("\"10.0 .0.1\"","7777")), "invalid \"ip\"");
    bad("port 0", wrap(entry("\"10.0.0.1\"","0")), "1-65535");
    bad("port 65536", wrap(entry("\"10.0.0.1\"","65536")), "1-65535");
    bad("port negative", wrap(entry("\"10.0.0.1\"","-5")), "1-65535");
    bad("port fractional", wrap(entry("\"10.0.0.1\"","7777.5")), "integer");
    bad("port text", wrap(entry("\"10.0.0.1\"","\"abc\"")), "number");
    bad("port missing", "{\"query\":[{\"name\":\"A\",\"ip\":\"10.0.0.1\"}]}", "missing \"port\"");
    bad("ip missing", "{\"query\":[{\"name\":\"A\",\"port\":7777}]}", "missing \"ip\"");
    bad("name missing", "{\"query\":[{\"ip\":\"10.0.0.1\",\"port\":7777}]}", "missing \"name\"");
    bad("name blank", "{\"query\":[{\"name\":\"  \",\"ip\":\"10.0.0.1\",\"port\":7777}]}", "\"name\" is empty");
    bad("ip is number", "{\"query\":[{\"name\":\"A\",\"ip\":12345,\"port\":7777}]}", "must be text");
    bad("maxplayers negative", "{\"query\":[{\"name\":\"A\",\"ip\":\"10.0.0.1\",\"port\":7777,\"maxplayers\":-1}]}", "maxplayers");
    bad("query empty array", "{\"query\":[]}", "empty");
    bad("query missing", "{\"servers\":[]}", "missing \"query\"");
    bad("query not array", "{\"query\":{}}", "missing \"query\"");
    bad("first entry not object", "{\"query\":[1,2]}", "not an object");
    bad("truncated/invalid JSON", "{\"query\":[{\"name\":", "not valid JSON");
    bad("HTML error page instead of JSON", "<html><body>502 Bad Gateway</body></html>", "not valid JSON");
    bad("empty body", "", "empty");
    bad("null body", null, "empty");
    bad("whitespace body", "   \n ", "empty");
    System.out.println("\nRESULT: pass="+pass+" fail="+fail);
    System.exit(fail==0?0:1);
  }
}
