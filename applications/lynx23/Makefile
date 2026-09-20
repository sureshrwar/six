include ../../Rules.guest

GUEST_CFLAGS += -DUNIX -DNO_KEYPAD -DNO_TTYTYPE -DSVR4 -DACCESS_AUTH \
		-DXMOSAIC_HACK -DVC=\"2.14\" \
		-IWWW/Library/Implementation -Isrc -I.

WWW_OBJS = \
	WWW/Library/Implementation/HTParse.o \
	WWW/Library/Implementation/HTAccess.o \
	WWW/Library/Implementation/HTTP.o \
	WWW/Library/Implementation/HTFile.o \
	WWW/Library/Implementation/HTBTree.o \
	WWW/Library/Implementation/HTFTP.o \
	WWW/Library/Implementation/HTTCP.o \
	WWW/Library/Implementation/SGML.o \
	WWW/Library/Implementation/HTMLDTD.o \
	WWW/Library/Implementation/HTChunk.o \
	WWW/Library/Implementation/HTPlain.o \
	WWW/Library/Implementation/HTWriter.o \
	WWW/Library/Implementation/HTMLGen.o \
	WWW/Library/Implementation/HTAtom.o \
	WWW/Library/Implementation/HTAnchor.o \
	WWW/Library/Implementation/HTStyle.o \
	WWW/Library/Implementation/HTList.o \
	WWW/Library/Implementation/HTString.o \
	WWW/Library/Implementation/HTRules.o \
	WWW/Library/Implementation/HTFormat.o \
	WWW/Library/Implementation/HTMIME.o \
	WWW/Library/Implementation/HTHistory.o \
	WWW/Library/Implementation/HTNews.o \
	WWW/Library/Implementation/HTGopher.o \
	WWW/Library/Implementation/HTTelnet.o \
	WWW/Library/Implementation/HTWSRC.o \
	WWW/Library/Implementation/HTAAUtil.o \
	WWW/Library/Implementation/HTAAServ.o \
	WWW/Library/Implementation/HTAABrow.o \
	WWW/Library/Implementation/HTAAFile.o \
	WWW/Library/Implementation/HTPasswd.o \
	WWW/Library/Implementation/HTGroup.o \
	WWW/Library/Implementation/HTACL.o \
	WWW/Library/Implementation/HTAuth.o \
	WWW/Library/Implementation/HTAAProt.o \
	WWW/Library/Implementation/HTAssoc.o \
	WWW/Library/Implementation/HTLex.o \
	WWW/Library/Implementation/HTUU.o

LYNX_OBJS = \
	src/LYClean.o \
	src/LYShowInfo.o \
	src/LYEdit.o \
	src/LYStrings.o \
	src/LYMail.o \
	src/HTAlert.o \
	src/GridText.o \
	src/LYGetFile.o \
	src/LYMain.o \
	src/LYMainLoop.o \
	src/LYCurses.o \
	src/LYBookmark.o \
	src/LYUtils.o \
	src/LYOptions.o \
	src/LYReadCFG.o \
	src/LYSearch.o \
	src/LYHistory.o \
	src/LYForms.o \
	src/LYPrint.o \
	src/LYrcFile.o \
	src/LYDownload.o \
	src/LYNews.o \
	src/LYKeymap.o \
	src/HTML.o \
	src/HTFWriter.o \
	src/HTInit.o \
	src/DefaultStyles.o \
	src/LYLocal.o \
	src/LYUpload.o

OBJS = $(WWW_OBJS) $(LYNX_OBJS)

all: lynx23

lynx23: $(OBJS) $(GUEST_CRT) $(GUEST_LIBCURSES) $(GUEST_LIBC)
	$(call guest_link_curses,$@,$(OBJS))

clean:
	rm -f WWW/Library/Implementation/*.o WWW/Library/Implementation/.*.o.d
	rm -f src/*.o src/.*.o.d
	rm -f lynx23
