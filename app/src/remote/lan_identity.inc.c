/* Minimal DER construction for an ephemeral TLS identity. P-256 generation,
 * SHA-256 and deterministic ECDSA are xrt APIs, never local crypto. The only
 * trust anchor is the exact certificate sent over the authenticated relay.
 * No key, grant, certificate or LAN address is written to disk. */
typedef struct MdoLanDer { uint8 Bytes[1024]; size_t Size; bool Failed; } MdoLanDer;
static void MdoLanDerBytes(MdoLanDer* Out, const void* Bytes, size_t Size)
{
    if (Out->Failed || Size > sizeof(Out->Bytes)-Out->Size) { Out->Failed = true; return; }
    memcpy(Out->Bytes+Out->Size,Bytes,Size); Out->Size += Size;
}
static void MdoLanDerValue(MdoLanDer* Out, uint8 Tag, const void* Bytes, size_t Size)
{
    uint8 Head[4] = {Tag,0,0,0}; size_t Count;
    if (Size < 128u) { Head[1]=(uint8)Size; Count=2u; }
    else if (Size < 256u) { Head[1]=0x81u; Head[2]=(uint8)Size; Count=3u; }
    else { Head[1]=0x82u; Head[2]=(uint8)(Size>>8u); Head[3]=(uint8)Size; Count=4u; }
    MdoLanDerBytes(Out,Head,Count); MdoLanDerBytes(Out,Bytes,Size);
}
static bool MdoLanCertificate(const uint8* Public, const uint8* SigningKey, bool Root, MdoLanDer* Final)
{
    static const uint8 Version[]={0xa0,3,2,1,2};
    static const uint8 Algorithm[]={0x30,10,6,8,0x2a,0x86,0x48,0xce,0x3d,4,3,2};
    static const uint8 Name[]={0x30,0x12,0x31,0x10,0x30,0x0e,6,3,0x55,4,3,0x0c,7,'m','d','o','-','l','a','n'};
    static const uint8 RootName[]={0x30,0x15,0x31,0x13,0x30,0x11,6,3,0x55,4,3,0x0c,10,'m','d','o','-','l','a','n','-','c','a'};
    static const uint8 KeyAlgorithm[]={0x30,0x13,6,7,0x2a,0x86,0x48,0xce,0x3d,2,1,6,8,0x2a,0x86,0x48,0xce,0x3d,3,1,7};
    static const uint8 San[]={0xa3,0x16,0x30,0x14,0x30,0x12,6,3,0x55,0x1d,0x11,4,0x0b,0x30,9,0x82,7,'m','d','o','-','l','a','n'};
    static const uint8 Basic[]={0x30,0x0f,6,3,0x55,0x1d,0x13,1,1,0xff,4,5,0x30,3,1,1,0xff};
    static const uint8 Usage[]={0x30,0x0e,6,3,0x55,0x1d,0x0f,1,1,0xff,4,4,3,2,2,0x84};
    uint8 Serial[17]={0}, Digest[32], Signature[80], KeyBits[66], SigBits[81];
    size_t SignatureSize=0u; bool Ok=false;
    MdoLanDer Tbs={0}, Validity={0}, Key={0}, Wrapped={0}, Cert={0}, Extensions={0}, ExtensionSequence={0}; xsha256 Hash;
    if (!xrtSecureRandom(Serial+1u,16u)) goto done;
    Serial[1] |= 0x80u; /* The leading sign octet must be required in DER. */
    MdoLanDerBytes(&Tbs,Version,sizeof(Version)); MdoLanDerValue(&Tbs,2u,Serial,sizeof(Serial));
    MdoLanDerBytes(&Tbs,Algorithm,sizeof(Algorithm)); MdoLanDerBytes(&Tbs,RootName,sizeof(RootName));
    MdoLanDerValue(&Validity,0x18u,"20000101000000Z",15u);
    MdoLanDerValue(&Validity,0x18u,"20990101000000Z",15u);
    MdoLanDerValue(&Tbs,0x30u,Validity.Bytes,Validity.Size);
    MdoLanDerBytes(&Tbs,Root ? RootName : Name,Root ? sizeof(RootName) : sizeof(Name));
    MdoLanDerBytes(&Key,KeyAlgorithm,sizeof(KeyAlgorithm)); KeyBits[0]=0u;
    /* xrt returns the SEC1 uncompressed point, including its 0x04 prefix. */
    memcpy(KeyBits+1u,Public,XRT_P256_PUBLIC_SIZE); MdoLanDerValue(&Key,3u,KeyBits,sizeof(KeyBits));
    MdoLanDerValue(&Tbs,0x30u,Key.Bytes,Key.Size);
    if (Root) { MdoLanDerBytes(&Extensions,Basic,sizeof(Basic)); MdoLanDerBytes(&Extensions,Usage,sizeof(Usage)); }
    else MdoLanDerBytes(&Extensions,San+4u,sizeof(San)-4u);
    MdoLanDerValue(&ExtensionSequence,0x30u,Extensions.Bytes,Extensions.Size);
    MdoLanDerValue(&Tbs,0xa3u,ExtensionSequence.Bytes,ExtensionSequence.Size);
    MdoLanDerValue(&Wrapped,0x30u,Tbs.Bytes,Tbs.Size); xrtSha256Init(&Hash);
    if (Tbs.Failed || Wrapped.Failed || !xrtSha256Update(&Hash,Wrapped.Bytes,Wrapped.Size) ||
        !xrtSha256Final(&Hash,Digest) || !xrtEcdsaP256SignDer(XCRYPTO_HASH_SHA256,Digest,SigningKey,Signature,sizeof(Signature),&SignatureSize)) goto done;
    MdoLanDerBytes(&Cert,Wrapped.Bytes,Wrapped.Size); MdoLanDerBytes(&Cert,Algorithm,sizeof(Algorithm));
    SigBits[0]=0u; memcpy(SigBits+1u,Signature,SignatureSize); MdoLanDerValue(&Cert,3u,SigBits,SignatureSize+1u);
    MdoLanDerValue(Final,0x30u,Cert.Bytes,Cert.Size); Ok=!Cert.Failed && !Final->Failed;
done:
    xrtSecureZero(Digest,sizeof(Digest)); return Ok;
}
static xtlsidentity* MdoLanIdentity(char Encoded[1537])
{
    uint8 RootKey[XRT_P256_PRIVATE_SIZE], LeafKey[XRT_P256_PRIVATE_SIZE];
    uint8 RootPublic[XRT_P256_PUBLIC_SIZE], LeafPublic[XRT_P256_PUBLIC_SIZE];
    MdoLanDer Root={0}, Leaf={0}; xtlsidentity* Identity=NULL; size_t Size=0u;
    if (!xrtP256KeyPair(RootKey,RootPublic) || !xrtP256KeyPair(LeafKey,LeafPublic) ||
        !MdoLanCertificate(RootPublic,RootKey,true,&Root) || !MdoLanCertificate(LeafPublic,RootKey,false,&Leaf)) goto done;
    xbytesview View={Leaf.Bytes,Leaf.Size}; Identity=xrtTlsIdentityP256(&View,1u,(xbytesview){LeafKey,sizeof(LeafKey)});
    if (Identity && !xrtBase64Encode(Root.Bytes,Root.Size,Encoded,1536u,&Size,NULL)) { xrtTlsIdentityRelease(Identity); Identity=NULL; }
    Encoded[Size]=0;
done:
    xrtSecureZero(RootKey,sizeof(RootKey)); xrtSecureZero(LeafKey,sizeof(LeafKey)); return Identity;
}
