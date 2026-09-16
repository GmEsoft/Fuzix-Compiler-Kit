all: lib6502.a crt0.o

OBJ =  __addysp.o __addyasp.o __subysp.o __asp.o __yasp.o \
	__lxaysptmp.o __asptmp.o \
       __lxa0sptmp.o __gloy.o __gloyl.o __lstxay.o __l_eq.o __l_eqc.o \
       __deref.o __derefc.o __castc_.o __asra.o \
       __minuseqtmp.o __minuseqtmpc.o  __minus.o __minusminus.o \
       __minusminusc.o __minusc.o \
       __divu.o __mulu.o __div.o __muluc.o \
       __plusplus.o __plus.o \
       __pluseqy.o __pluseqtmp.o __pluseqtmpc.o __plusplusc.o \
       __poptmp.o __poptmpc.o __pushc.o __pushl.o __push.o __ytmp.o __ytmpc.o \
       __poptmpstxa.o __pop.o \
       __callfunc.o __switchc.o __switch.o __switchl.o \
       zeropage.o __eqop.o __negate.o \
       __bool.o __boolc.o __not.o __notc.o __eqeqtmp.o __netmp.o \
       __eqeqtmpc.o __netmpc.o \
       __lttmp.o __lttmpu.o __gteqtmp.o  __gteqtmpu.o \
       __lttmpc.o __lttmpuc.o __gteqtmpc.o __gteqtmpuc.o \
       __lstmp.o __rstmp.o __shlc.o \
       __lshift1.o __lshift2.o __lshift3.o __lshift4.o \
       __shleqc.o __shleq.o  __shrequc.o __shrequ.o \
       __cpl.o \
       __cast_l.o __booll.o __notl.o __derefl.o __assignl.o __cpll.o \
       __pop32.o __bandl.o __orl.o __xorl.o __lcmp.o __negatel.o __minusl.o \
       __plusl.o __pluseql.o __postincl.o __minuseql.o __postdecl.o \
       __andeql.o __oreql.o __xoreql.o \
       __shll.o __shrl.o __sheqlop.o \
       __lmul.o __ldiv.o \
      __boolf.o __cast2f.o __castf.o __cceqf.o __ccgteqf.o __ccgtf.o __cclteqf.o \
      __ccltf.o __ccnef.o __divf.o __minusf.o __mulf.o __plusf.o __eqopf.o \
      __lderef.o __lderefc.o __lderefya.o __lderefcya.o \
      __reg.o __reql1.o __reql2.o __reql3.o __reql4.o \
      __ldreg1y.o __ldreg2y.o __ldreg3y.o __ldreg4y.o \
      __streg1y.o __streg2y.o __streg3y.o __streg4y.o \
      __sbctmpy.o __sbctmp.o

GEN =  __adcspy.o __adctmpy.o __sbcspy.o __andspy.o __andtmpy.o __eorspy.o __eortmpy.o \
       __ldaspy.o __ldatmpy.o __oraspy.o __oratmpy.o __staspy.o __statmpy.o \
       __adctmp.o __andtmp.o __oratmp.o __eortmp.o \
       __adceqtmp.o __andeqtmp.o __oraeqtmp.o __eoreqtmp.o \
       __adceqtmpc.o __andeqtmpc.o __eoreqtmpc.o __oraeqtmpc.o \
       __gloy1.o __gloy2.o __gloy3.o __gloy4.o \
       __gloy5.o __gloy6.o __gloy7.o __gloy8.o \
       __gloy9.o __gloy10.o __gloy11.o __gloy12.o \
       __gloy13.o __gloy14.o __gloy15.o __gloy16.o \
       __gloytmp1.o __gloytmp2.o __gloytmp3.o __gloytmp4.o \
       __gloytmp5.o __gloytmp6.o __gloytmp7.o __gloytmp8.o \
       __gloytmp9.o __gloytmp10.o __gloytmp11.o __gloytmp12.o \
       __gloytmp13.o __gloytmp14.o __gloytmp15.o __gloytmp16.o \
       __pushly1.o __pushly2.o __pushly3.o __pushly4.o \
       __pushly5.o __pushly6.o __pushly7.o __pushly8.o \
       __pushly9.o __pushly10.o __pushly11.o __pushly12.o \
       __pushly13.o __pushly14.o __pushly15.o __pushly16.o
    
GENSRC = __adcspy.s __adctmpy.s __sbcspy.s __andspy.s __andtmpy.s __eorspy.s __eortmpy.s \
       __ldaspy.s __ldatmpy.s __oraspy.s __oratmpy.s __staspy.s __statmpy.s \
       __adctmp.s __andtmp.s __oratmp.s __eortmp.s \
       __adceqtmp.s __andeqtmp.s __oraeqtmp.s __eoreqtmp.s \
       __adceqtmpc.s __andeqtmpc.s __eoreqtmpc.s __oraeqtmpc.s \
       __gloy1.s __gloy2.s __gloy3.s __gloy4.s \
       __gloy5.s __gloy6.s __gloy7.s __gloy8.s \
       __gloy9.s __gloy10.s __gloy11.s __gloy12.s \
       __gloy13.s __gloy14.s __gloy15.s __gloy16.s \
       __gloytmp1.s __gloytmp2.s __gloytmp3.s __gloytmp4.s \
       __gloytmp5.s __gloytmp6.s __gloytmp7.s __gloytmp8.s \
       __gloytmp9.s __gloytmp10.s __gloytmp11.s __gloytmp12.s \
       __gloytmp13.s __gloytmp14.s __gloytmp15.s __gloytmp16.s \
       __pushly1.s __pushly2.s __pushly3.s __pushly4.s \
       __pushly5.s __pushly6.s __pushly7.s __pushly8.s \
       __pushly9.s __pushly10.s __pushly11.s __pushly12.s \
       __pushly13.s __pushly14.s __pushly15.s __pushly16.s
    
makeops: makeops.c
	$(CC) makeops.c -o makeops

.s.o:
	fcc -m6502 -c $<

.c.o:
	fcc -m6502 -O -c $<

$(GENSRC): makeops
	./makeops

lib6502.a: $(OBJ) $(GEN)
	rm -f lib6502.a
	ar qc lib6502.a `../lorderz80 $(OBJ) $(GEN) | tsort`

clean:
	rm -f *.o *.a  $(GEN) *~ makeops
