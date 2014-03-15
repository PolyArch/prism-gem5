

microcode = '''

def macroop SIGMOIDSS_XMM_XMM {
    mrsqrt xmml, xmmlm, size=4, ext=Scalar
};

def macroop SIGMOIDSS_XMM_M {
    ldfp ufp1, seg, sib, disp, dataSize=4
    mrsqrt xmml, ufp1, size=4, ext=Scalar
};

def macroop SIGMOIDSS_XMM_P {
    rdip t7
    ldfp ufp1, seg, riprel, disp, dataSize=4
    mrsqrt xmml, ufp1, size=4, ext=Scalar
};

'''
