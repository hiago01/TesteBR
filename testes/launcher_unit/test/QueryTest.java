package test;
import com.samp.mobile.launcher.util.SampQueryAPI;
import java.net.*; import java.nio.*; import java.nio.charset.Charset;
public class QueryTest {
  static int pass=0, fail=0;
  static void r(String n, boolean g, String d){ if(g)pass++; else fail++; System.out.println((g?"PASS ":"FAIL ")+n+(g?"":" -> "+d)); }
  /** mode: 0 = correto, 1 = 'i' com tamanho de string malicioso, 2 = ping ok mas 'i' sem resposta, 3 = ping com eco errado */
  static Thread server(final DatagramSocket s, final int mode, final int delayMs){
    Thread t = new Thread(() -> { try { while(true){ byte[] b=new byte[512]; DatagramPacket p=new DatagramPacket(b,b.length); s.receive(p);
        byte op=b[10]; byte[] out;
        if(delayMs>0) Thread.sleep(delayMs);
        if(op=='p'){ out=new byte[15]; System.arraycopy(b,0,out,0,15); if(mode==3) out[11]^=0x55; }
        else { if(mode==2) continue; ByteBuffer bb=ByteBuffer.allocate(300).order(ByteOrder.LITTLE_ENDIAN); bb.put(b,0,10); bb.put((byte)'i'); bb.put((byte)1); bb.putShort((short)37); bb.putShort((short)100);
          Charset c=Charset.forName("windows-1251"); byte[] h="Maximo RP".getBytes(c), m="Roleplay".getBytes(c), l="Portugues".getBytes(c);
          if(mode==1){ bb.putInt(0x7fffff00); } else { bb.putInt(h.length); bb.put(h); bb.putInt(m.length); bb.put(m); bb.putInt(l.length); bb.put(l);} 
          out=java.util.Arrays.copyOf(bb.array(), bb.position()); }
        s.send(new DatagramPacket(out,out.length,p.getAddress(),p.getPort())); } } catch(Exception e){} });
    t.setDaemon(true); t.start(); return t; }
  public static void main(String[] a) throws Exception {
    DatagramSocket srv=new DatagramSocket(0, InetAddress.getByName("127.0.0.1")); int port=srv.getLocalPort(); server(srv,0,30);
    SampQueryAPI q=new SampQueryAPI("127.0.0.1",port);
    boolean up=q.mo7166d();
    r("server up: ping reply validated", up, "false");
    r("real RTT measured (>=25ms because server delays 30ms)", q.lastPingMs>=25 && q.lastPingMs<1000, "ping="+q.lastPingMs);
    String[] i=q.mo7164b();
    r("info parsed", i!=null && i[1].equals("37") && i[2].equals("100") && i[3].equals("Maximo RP") && i[4].equals("Roleplay") && i[5].equals("Portugues") && i[0].equals("1"), i==null?"null":String.join("|",i));
    q.f7277a.close(); srv.close();

    // sem servidor: porta fechada -> timeout, nunca "online"
    DatagramSocket dead=new DatagramSocket(0, InetAddress.getByName("127.0.0.1")); int dp=dead.getLocalPort(); dead.close();
    long t0=System.currentTimeMillis(); SampQueryAPI q2=new SampQueryAPI("127.0.0.1",dp);
    boolean up2=q2.mo7166d(); long el=System.currentTimeMillis()-t0;
    r("no server: NOT online", !up2, "returned true");
    r("no server: ping stays -1", q2.lastPingMs==-1, "ping="+q2.lastPingMs);
    r("no server: returns within timeout (<4s)", el<4000, "elapsed="+el);
    r("no server: info() == null (no zero-buffer fake data)", q2.mo7164b()==null, "non-null");
    q2.f7277a.close();

    // servidor que ignora silenciosamente (porta aberta, nada responde)
    DatagramSocket mute=new DatagramSocket(0, InetAddress.getByName("127.0.0.1"));
    SampQueryAPI q3=new SampQueryAPI("127.0.0.1",mute.getLocalPort());
    r("silent server: NOT online", !q3.mo7166d(), "returned true"); q3.f7277a.close(); mute.close();

    // eco errado no ping
    DatagramSocket s4=new DatagramSocket(0, InetAddress.getByName("127.0.0.1")); server(s4,3,0);
    SampQueryAPI q4=new SampQueryAPI("127.0.0.1",s4.getLocalPort());
    r("wrong ping echo: NOT online", !q4.mo7166d(), "returned true"); q4.f7277a.close(); s4.close();

    // ping ok, info sem resposta
    DatagramSocket s5=new DatagramSocket(0, InetAddress.getByName("127.0.0.1")); server(s5,2,0);
    SampQueryAPI q5=new SampQueryAPI("127.0.0.1",s5.getLocalPort());
    boolean pong=q5.mo7166d(); String[] info5=q5.mo7164b();
    r("ping ok but info times out -> info null", pong && info5==null, "pong="+pong+" info="+(info5==null?"null":"non-null")); q5.f7277a.close(); s5.close();

    // tamanho de string malicioso (2 GB): sem OOM, retorna null
    DatagramSocket s6=new DatagramSocket(0, InetAddress.getByName("127.0.0.1")); server(s6,1,0);
    SampQueryAPI q6=new SampQueryAPI("127.0.0.1",s6.getLocalPort());
    String[] info6=q6.mo7164b();
    r("malicious string length: null, no OOM", info6==null, "non-null"); q6.f7277a.close(); s6.close();

    // host invalido
    SampQueryAPI q7=new SampQueryAPI("host.invalid.",7777);
    r("unresolvable host: flagged unusable", !q7.f7282f && !q7.mo7166d(), "usable"); if(q7.f7277a!=null)q7.f7277a.close();

    System.out.println("\nRESULT: pass="+pass+" fail="+fail); System.exit(fail==0?0:1);
  }
}
