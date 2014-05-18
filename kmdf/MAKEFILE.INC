_LNG=$(LANGUAGE)
STAMP=stampinf -f $@ -a $(_BUILDARCH) -k $(KMDF_VERSION_MAJOR).$(KMDF_VERSION_MINOR)

$(OBJ_PATH)\$(O)\$(INF_NAME1).inf: $(INF_NAME1).inx 
    copy $(@B).inx $@
    $(STAMP)

$(OBJ_PATH)\$(O)\pcidrv.bmf : pcidrv.mof
    mofcomp -B:$@ pcidrv.mof
    wmimofck $@   








