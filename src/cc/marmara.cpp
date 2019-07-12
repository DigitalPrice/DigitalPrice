/******************************************************************************
/******************************************************************************
 * Copyright © 2014-2019 The SuperNET Developers.                             *
 *                                                                            *
 * See the AUTHORS, DEVELOPER-AGREEMENT and LICENSE files at                  *
 * the top-level directory of this distribution for the individual copyright  *
 * holder information and the developer policies on copyright and licensing.  *
 *                                                                            *
 * Unless otherwise agreed in a custom licensing agreement, no part of the    *
 * SuperNET software, including this file may be copied, modified, propagated *
 * or distributed except according to the terms contained in the LICENSE file *
 *                                                                            *
 * Removal or modification of this copyright notice is prohibited.            *
 *                                                                            *
 ******************************************************************************/

#include "CCMarmara.h"
#include "key_io.h"

 /*
  Marmara CC is for the MARMARA project

  'С', 'R' request for credit issuance ('C' is the creation tx containing initial data for credit loop)
  vins normal
  vout0 request to senderpk (issuer or endorser)

  'I' check issuance
  vin0 request from 'R'
  vins1+ normal
  vout0 baton to 1st receiverpk
  vout1 marker to Marmara so all issuances can be tracked (spent when loop is closed)

  'T' check transfer to endorser
  vin0 request from 'R'
  vin1 baton from 'I'/'T'
  vins2+ normal
  vout0 baton to next receiverpk (following the unspent baton back to original is the credit loop)

  'S' check settlement
  vin0 'I' marker
  vin1 baton
  vins CC utxos from credit loop

  'D' default/partial payment

  'L' lockfunds

  'K' pubkey in cc vout opret who locked his funds in loop 

 */

const int32_t MARMARA_MARKER_VOUT = 1;


// credit loop data from different tx oprets
struct CreditLoopOpret {
    bool hasCreateOpret;
    bool hasIssuanceOpret;
    bool hasSettlementOpret;

    uint8_t autoSettlement;
    uint8_t autoInsurance;

    // create tx data:
    CAmount amount;  // loop amount
    int32_t matures; // check maturing height
    std::string currency;  // currently MARMARA

    // issuer data:
    int32_t disputeExpiresHeight;
    uint8_t escrowOn;
    CAmount blockageAmount;

    // last issuer/endorser/receiver data:
    uint256 createtxid;
    CPubKey createpk;       // first pk. We need this var to make sure MarmaraDecodeLoopOpret does not override the value in pk.
    CPubKey pk;             // may be either sender or receiver pk
    int32_t avalCount;      // only for issuer/endorser

    // settlement data:
    CAmount remaining;

    // init default values:
    CreditLoopOpret() {
        hasCreateOpret = false;
        hasIssuanceOpret = false;
        hasSettlementOpret = false;

        amount = 0LL;
        matures = 0;
        autoSettlement = 1;
        autoInsurance = 1;

        createtxid = zeroid;
        disputeExpiresHeight = 0;
        avalCount = 0;
        escrowOn = false;
        blockageAmount = 0LL;

        remaining = 0L;
    }
};


 // start of consensus code

int64_t IsMarmaravout(struct CCcontract_info *cp, const CTransaction& tx, int32_t v)
{
    char destaddr[KOMODO_ADDRESS_BUFSIZE];
    if (tx.vout[v].scriptPubKey.IsPayToCryptoCondition() != 0)
    {
        if (Getscriptaddress(destaddr, tx.vout[v].scriptPubKey) && strcmp(destaddr, cp->unspendableCCaddr) == 0)
            return(tx.vout[v].nValue);
    }
    return(0);
}

// Get randomized within range [3 month...2 year] using ind as seed(?)
/* not used now
int32_t MarmaraRandomize(uint32_t ind)
{
    uint64_t val64; uint32_t val, range = (MARMARA_MAXLOCK - MARMARA_MINLOCK);
    val64 = komodo_block_prg(ind);
    val = (uint32_t)(val64 >> 32);
    val ^= (uint32_t)val64;
    return((val % range) + MARMARA_MINLOCK);
}
*/

// get random but fixed for the height param unlock height within 3 month..2 year interval  -- discontinued
// now always returns maxheight
int32_t MarmaraUnlockht(int32_t height)
{
/*  uint32_t ind = height / MARMARA_GROUPSIZE;
    height = (height / MARMARA_GROUPSIZE) * MARMARA_GROUPSIZE;
    return(height + MarmaraRandomize(ind)); */
    return MARMARA_V2LOCKHEIGHT;
}

uint8_t MarmaraDecodeCoinbaseOpret(const CScript scriptPubKey, CPubKey &pk, int32_t &height, int32_t &unlockht)
{
    vscript_t vopret; uint8_t *script, e, f, funcid;
    GetOpReturnData(scriptPubKey, vopret);
    script = (uint8_t *)vopret.data();
    
    /* if (0)
    {
        int32_t i;

        std::cerr  << " ";
        for (i = 0; i < vopret.size(); i++)
            fprintf(stderr, "%02x", script[i]);
        fprintf(stderr, " <- opret\n");
    } */
    if (vopret.size() > 2)
    {
        if (script[0] == EVAL_MARMARA)
        {
            if (script[1] == 'C' || script[1] == 'P' || script[1] == 'L')
            {
                if (E_UNMARSHAL(vopret, ss >> e; ss >> f; ss >> pk; ss >> height; ss >> unlockht) != 0)
                {
                    return(script[1]);
                }
                else
                    LOGSTREAMFN("marmara", CCLOG_ERROR, stream  << "unmarshal error for funcid=" << (char)script[1] << std::endl);
            }
            //else 
            //  fprintf(stderr,"%s script[1] is %d != 'C' %d or 'P' %d or 'L' %d\n", logFuncName, script[1],'C','P','L');
        }
        else
            LOGSTREAMFN("marmara", CCLOG_ERROR, stream  << " not my opret, funcid=" << (int)script[0] << std::endl);
    }
    else 
        LOGSTREAMFN("marmara", CCLOG_ERROR, stream  << " bad opret, vopret.size() is " << vopret.size() << std::endl);
    return(0);
}

CScript EncodeMarmaraCoinbaseOpRet(uint8_t funcid, CPubKey pk, int32_t ht)
{
    CScript opret; int32_t unlockht; uint8_t evalcode = EVAL_MARMARA;
    unlockht = MarmaraUnlockht(ht);
    opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << pk << ht << unlockht);
 /*   if (0)
    {
        vscript_t vopret; uint8_t *script, i;
        GetOpReturnData(opret, vopret);
        script = (uint8_t *)vopret.data();
        {
            std::cerr  << " ";
            for (i = 0; i < vopret.size(); i++)
                fprintf(stderr, "%02x", script[i]);
            fprintf(stderr, " <- gen opret.%c\n", funcid);
        }
    } */
    return(opret);
}


// encode lock-in-loop tx opret functions:

CScript MarmaraEncodeLoopCreateOpret(CPubKey senderpk, int64_t amount, int32_t matures, std::string currency)
{
    CScript opret; 
    uint8_t evalcode = EVAL_MARMARA;
    uint8_t funcid = 'C'; // create tx (initial request tx)
    uint8_t version = 1;

    opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << version << senderpk << amount << matures << currency);
    return(opret);
}

CScript MarmaraEncodeLoopIssuerOpret(uint256 createtxid, CPubKey receiverpk, uint8_t autoSettlement, uint8_t autoInsurance, int32_t avalCount, int32_t disputeExpiresHeight, uint8_t escrowOn, CAmount blockageAmount )
{
    CScript opret;
    uint8_t evalcode = EVAL_MARMARA;
    uint8_t funcid = 'I'; // issuance tx
    uint8_t version = 1;

    opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << version << createtxid << receiverpk << autoSettlement << autoInsurance << avalCount << disputeExpiresHeight << escrowOn << blockageAmount);
    return(opret);
}

CScript MarmaraEncodeLoopRequestOpret(uint256 createtxid, CPubKey senderpk)
{
    CScript opret;
    uint8_t evalcode = EVAL_MARMARA;
    uint8_t funcid = 'R'; // request tx
    uint8_t version = 1;

    opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << version << createtxid << senderpk);
    return(opret);
}

CScript MarmaraEncodeLoopTransferOpret(uint256 createtxid, CPubKey receiverpk, int32_t avalCount)
{
    CScript opret;
    uint8_t evalcode = EVAL_MARMARA;
    uint8_t funcid = 'T'; // transfer tx
    uint8_t version = 1;

    opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << version << createtxid << receiverpk << avalCount);
    return(opret);
}

CScript MarmaraEncodeLoopCCVoutOpret(uint256 createtxid, CPubKey senderpk)
{
    CScript opret;
    uint8_t evalcode = EVAL_MARMARA;
    uint8_t funcid = 'K'; // opret in cc 1of2 lock-in-loop vout
    uint8_t version = 1;

    opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << version << createtxid << senderpk);
    return(opret);
}

CScript MarmaraEncodeLoopSettlementOpret(bool isSuccess, uint256 createtxid, CPubKey pk, CAmount remaining)
{
    CScript opret;
    uint8_t evalcode = EVAL_MARMARA;
    uint8_t funcid = isSuccess ? 'S' : 'D'; 
    uint8_t version = 1;

    opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << version << createtxid << pk << remaining);
    return(opret);
}

// decode different lock-in-loop oprets, update the loopData
uint8_t MarmaraDecodeLoopOpret(const CScript scriptPubKey, struct CreditLoopOpret &loopData)
{
    vscript_t vopret; 
    const uint8_t versionSupported = 1;

    GetOpReturnData(scriptPubKey, vopret);
    if (vopret.size() >= 3) 
     {
        uint8_t evalcode = vopret.begin()[0];
        uint8_t funcid = vopret.begin()[1];
        uint8_t version = vopret.begin()[2];

        if (evalcode == EVAL_MARMARA)   // check limits
        {
            if (version != versionSupported) 
            {
                if (funcid == 'C') {  // createtx
                    if (E_UNMARSHAL(vopret, ss >> evalcode; ss >> funcid; ss >> version; ss >> loopData.createpk; ss >> loopData.amount; ss >> loopData.matures; ss >> loopData.currency) != 0) {
                        loopData.hasCreateOpret = true;
                        return funcid;
                    }
                }
                else if (funcid == 'I') {
                    if (E_UNMARSHAL(vopret, ss >> evalcode; ss >> funcid; ss >> version; ss >> loopData.createtxid; ss >> loopData.pk; ss >> loopData.autoSettlement; ss >> loopData.autoInsurance; ss >> loopData.avalCount >> loopData.disputeExpiresHeight >> loopData.escrowOn >> loopData.blockageAmount) != 0) {
                        loopData.hasIssuanceOpret = true;
                        return funcid;
                    }
                }
                else if (funcid == 'R') {
                    if (E_UNMARSHAL(vopret, ss >> evalcode; ss >> funcid; ss >> version; ss >> loopData.createtxid; ss >> loopData.pk) != 0) {
                        return funcid;
                    }
                }
                else if (funcid == 'T') {
                    if (E_UNMARSHAL(vopret, ss >> evalcode; ss >> funcid; ss >> version; ss >> loopData.createtxid; ss >> loopData.pk; ss >> loopData.avalCount) != 0) {
                        return funcid;
                    }
                }
                else if (funcid == 'K') {
                    if (E_UNMARSHAL(vopret, ss >> evalcode; ss >> funcid; ss >> version; ss >> loopData.createtxid; ss >> loopData.pk) != 0) {
                        return funcid;
                    }
                }
                else if (funcid == 'S' || funcid == 'D') {
                    if (E_UNMARSHAL(vopret, ss >> evalcode; ss >> funcid; ss >> version; ss >> loopData.createtxid; ss >> loopData.pk >> loopData.remaining) != 0) {
                        loopData.hasSettlementOpret = true;
                        return funcid;
                    }
                }
                // get here from any E_UNMARSHAL error:
                LOGSTREAMFN("marmara", CCLOG_ERROR, stream << "cannot parse opret=" << HexStr(vopret) << std::endl);
            }
            else
                LOGSTREAMFN("marmara", CCLOG_ERROR, stream << "unsupported opret version=" << (int)version << std::endl);
        }
        else
            LOGSTREAMFN("marmara", CCLOG_ERROR, stream << "not marmara opret, evalcode=" << (int)evalcode << std::endl);
    }
    else
        LOGSTREAMFN("marmara", CCLOG_ERROR, stream << "opret too small=" << HexStr(vopret) << std::endl);

    return(0);
}




// finds the creation txid from the loop tx opret or 
// return itself if it is the request tx
int32_t MarmaraGetcreatetxid(uint256 &createtxid, uint256 txid)
{
    CTransaction tx; 
    uint256 hashBlock; 
  
    createtxid = zeroid;
    if (myGetTransaction(txid, tx, hashBlock) != 0 && !hashBlock.IsNull() && tx.vout.size() > 1)  // might be called from validation code, so non-locking version
    {
        uint8_t funcid;
        struct CreditLoopOpret loopData;

        if ((funcid = MarmaraDecodeLoopOpret(tx.vout.back().scriptPubKey, loopData)) == 'I' || funcid == 'T' || funcid == 'R' ) {
            createtxid = loopData.createtxid;
            LOGSTREAMFN("marmara", CCLOG_DEBUG2, stream  << " found for funcid=I,T,R createtxid=" << createtxid.GetHex() << std::endl);
            return(0);
        }
        else if (funcid == 'C')
        {
            if (createtxid == zeroid)
                createtxid = txid;
            LOGSTREAMFN("marmara", CCLOG_DEBUG2, stream  << " found for funcid=C createtxid=" << createtxid.GetHex() << std::endl);
            return(0);
        }
    }
    return(-1);
}

// finds the latest batontxid starting from any baton txid
// adds createtxid in creditloop vector
// finds all the baton txids starting from the createtx (1+ in creditloop vector), apart from the latest baton txid
// returns the number of txns marked with the baton plus  1 (createtxid)
int32_t MarmaraGetbatontxid(std::vector<uint256> &creditloop, uint256 &batontxid, uint256 txid)
{
    uint256 createtxid, spenttxid; 
    int64_t value; 
    int32_t vini, height, n = 0;
    const int32_t nbatonvout = 0;
    
    batontxid = zeroid;
    if (MarmaraGetcreatetxid(createtxid, txid) == 0) // retrieve the initial creation txid
    {
        txid = createtxid;
        //fprintf(stderr,"%s txid.%s -> createtxid %s\n", logFuncName, txid.GetHex().c_str(),createtxid.GetHex().c_str());
        while (CCgetspenttxid(spenttxid, vini, height, txid, nbatonvout) == 0)
        {
            creditloop.push_back(txid);
            //fprintf(stderr,"%d: %s\n",n,txid.GetHex().c_str());
            n++;
            if ((value = CCgettxout(spenttxid, nbatonvout, 1, 1)) == 10000)
            {
                batontxid = spenttxid;
                //fprintf(stderr,"%s got baton %s %.8f\n", logFuncName, batontxid.GetHex().c_str(),(double)value/COIN);
                return(n);
            }
            else if (value > 0)
            {
                batontxid = spenttxid;
                LOGSTREAMFN("marmara", CCLOG_ERROR, stream  << " n=" << n << " got and will use false baton=" << batontxid.GetHex() << " vout=" << nbatonvout << "value=" << (double)value / COIN << std::endl);
                return(n);
            }
            // TODO: get funcid (and check?)
            txid = spenttxid;
        }
    }
    return(-1);
}

// load the create tx and adds data from its opret to loopData safely, with no overriding
int32_t MarmaraGetLoopCreateData(uint256 createtxid, struct CreditLoopOpret &loopData)
{
    CTransaction tx;
    uint256 hashBlock;

    if (myGetTransaction(createtxid, tx, hashBlock) != 0 && !hashBlock.IsNull() && tx.vout.size() > 1)  // might be called from validation code, so non-locking version
    {
        uint8_t funcid;
        struct CreditLoopOpret loopData;

        if ((funcid = MarmaraDecodeLoopOpret(tx.vout.back().scriptPubKey, loopData)) == 'C') {
            return(0);
        }
    }
    return(-1);
}

// returns scriptPubKey with 1of2 addr for coinbase tx where coins will go in createNewBlock in miner.cpp 
CScript Marmara_scriptPubKey(int32_t height, CPubKey pk)
{
    CTxOut ccvout; struct CCcontract_info *cp, C; 
    CPubKey Marmarapk;

    cp = CCinit(&C, EVAL_MARMARA);
    Marmarapk = GetUnspendable(cp, 0);
    if (height > 0 && (height & 1) == 0 && pk.size() == 33)
    {
        ccvout = MakeCC1of2vout(EVAL_MARMARA, 0, Marmarapk, pk);
        char coinaddr[KOMODO_ADDRESS_BUFSIZE];
        Getscriptaddress(coinaddr, ccvout.scriptPubKey);
        LOGSTREAMFN("marmara", CCLOG_INFO, stream  << "for activated rewards using pk=" << HexStr(pk) << " height=" << height << " 1of2addr=" << coinaddr << std::endl);
    }
    else
        LOGSTREAMFN("marmara", CCLOG_DEBUG1, stream  << "not even ht, returning empty scriptPubKey" << std::endl);
    return(ccvout.scriptPubKey);
}

// set marmara coinbase opret for even blocks
CScript MarmaraCoinbaseOpret(uint8_t funcid, int32_t height, CPubKey pk)
{
    uint8_t *ptr;
    //fprintf(stderr,"height.%d pksize.%d\n",height,(int32_t)pk.size());
    if (height > 0 && (height & 1) == 0 && pk.size() == 33)
        return(EncodeMarmaraCoinbaseOpRet(funcid, pk, height));
    else
        return(CScript());
}

// half of the blocks (with even heights) should be mined as activated (to some unlock height)
// validates opreturn for even blocks
int32_t MarmaraValidateCoinbase(int32_t height, CTransaction tx, std::string &errmsg)
{ 
/*    if (0) // not used
    {
        int32_t d, histo[365 * 2 + 30];
        memset(histo, 0, sizeof(histo));
        for (ht = 2; ht < 100; ht++)
            fprintf(stderr, "%d ", MarmaraUnlockht(ht));
        fprintf(stderr, " <- first 100 unlock heights\n");
        for (ht = 2; ht < 1000000; ht += MARMARA_GROUPSIZE)
        {
            d = (MarmaraUnlockht(ht) - ht) / 1440;
            if (d < 0 || d > sizeof(histo) / sizeof(*histo))
                fprintf(stderr, "d error.%d at ht.%d\n", d, ht);
            else histo[d]++;
        }

        std::cerr  << " ";
        for (ht = 0; ht < sizeof(histo) / sizeof(*histo); ht++)
            fprintf(stderr, "%d ", histo[ht]);
        fprintf(stderr, "<- unlock histogram[%d] by days locked\n", (int32_t)(sizeof(histo) / sizeof(*histo)));
    } */

    if ((height & 1) != 0) // odd block - no marmara opret
    {
        return(0);  // TODO: do we need to check here that really no marmara coinbase opret for odd blocks?
    }
    else //even block - check for cc vout & opret
    {
        struct CCcontract_info *cp, C; CPubKey Marmarapk, pk; int32_t ht, unlockht; CTxOut ccvout;
        cp = CCinit(&C, EVAL_MARMARA);
        Marmarapk = GetUnspendable(cp, 0);

        if (tx.vout.size() == 2 && tx.vout[1].nValue == 0)
        {
            if (MarmaraDecodeCoinbaseOpret(tx.vout[1].scriptPubKey, pk, ht, unlockht) == 'C')
            {
                if (ht == height && MarmaraUnlockht(height) == unlockht)
                {
                    //fprintf(stderr,"ht.%d -> unlock.%d\n",ht,unlockht);
                    ccvout = MakeCC1of2vout(EVAL_MARMARA, 0, Marmarapk, pk);
                    if (ccvout.scriptPubKey == tx.vout[0].scriptPubKey)
                        return(0);
                    char addr0[KOMODO_ADDRESS_BUFSIZE], addr1[KOMODO_ADDRESS_BUFSIZE];
                    Getscriptaddress(addr0, ccvout.scriptPubKey);
                    Getscriptaddress(addr1, tx.vout[0].scriptPubKey);
                    LOGSTREAMFN("marmara", CCLOG_ERROR, stream << " ht=" << height << " mismatched CCvout scriptPubKey=" << addr0 << " vs tx.vout[0].scriptPubKey=" << addr1 << " pk.size=" << pk.size() << " pk=" << HexStr(pk) << std::endl);
                }
                else
                    LOGSTREAMFN("marmara", CCLOG_ERROR, stream << " ht=" << height << " MarmaraUnlockht=" << MarmaraUnlockht(height) << " vs opret's ht=" << ht << " unlock=" << unlockht << std::endl);
            }
            else
                LOGSTREAMFN("marmara", CCLOG_ERROR, stream << " ht=" << height << " error decoding coinbase opret" << std::endl);
        }
        else
            LOGSTREAMFN("marmara", CCLOG_ERROR, stream << " ht=" << height << " incorrect vout size for marmara coinbase" << std::endl);

        errmsg = "marmara cc constrains even height blocks to pay 100%% to CC in vout0 with opreturn";
        return(-1);
    }
}

// check stake tx
// stake tx should have 1 cc vout and opret
// stake tx points to staking utxo
// stake tx vout[0].scriptPubKey equals the referred staking utxo scriptPubKey and opret equals to the referred opret in the last vout of the staking utxo
// see komodo_staked where stake tx is created
int32_t MarmaraPoScheck(char *destaddr, CScript inOpret, CTransaction staketx)  // note: the opret is fetched in komodo_txtime from cc opret or the last vout. 
                                                                                // And that opret was added to stake tx by MarmaraSignature()
{
    uint8_t funcid; 
    char coinaddr[KOMODO_ADDRESS_BUFSIZE]; 

    LOGSTREAMFN("marmara", CCLOG_DEBUG2, stream  << " staketxid=" << staketx.GetHash().ToString() << " numvins=" << staketx.vin.size() << " numvouts=" << staketx.vout.size() << " val="  << (double)staketx.vout[0].nValue / COIN  << " inOpret.size=" << inOpret.size() << std::endl);
    if (staketx.vout.size() == 2 && inOpret == staketx.vout[1].scriptPubKey)
    {
        CScript opret;
        struct CCcontract_info *cp, C;
        cp = CCinit(&C, EVAL_MARMARA);
        CPubKey Marmarapk = GetUnspendable(cp, 0);
        CPubKey senderpk;

        if (CheckEitherOpRet(IsActivatedOpret, staketx, 0, opret, senderpk))
        {
            //int32_t height, unlockht;
            //funcid = DecodeMarmaraCoinbaseOpRet(opret, senderpk, height, unlockht);
            GetCCaddress1of2(cp, coinaddr, Marmarapk, senderpk);

            bool isEqualAddr = (strcmp(destaddr, coinaddr) == 0);
            //LOGSTREAMFN("marmara", CCLOG_INFO, stream << "found activated opret" << (funcid ? (char)funcid : ' ') << " ht=" << height << " unlock=" << unlockht << " addr=" << coinaddr << " isEqualAddr=" << isEqualAddr << std::endl);
            LOGSTREAMFN("marmara", CCLOG_INFO, stream << "found activated opret" << " addr=" << coinaddr << " isEqualAddr=" << isEqualAddr << std::endl);
            return isEqualAddr ? 1 : 0;
        }
        else if (CheckEitherOpRet(IsLockInLoopOpret, staketx, 0, opret, senderpk))
        {
            struct CreditLoopOpret loopData;

            MarmaraDecodeLoopOpret(opret, loopData);

            char txidaddr[KOMODO_ADDRESS_BUFSIZE];
            CPubKey createtxidPk = CCtxidaddr(txidaddr, loopData.createtxid);

            GetCCaddress1of2(cp, coinaddr, Marmarapk, createtxidPk);

            bool isEqualAddr = (strcmp(destaddr, coinaddr) == 0);
            LOGSTREAMFN("marmara", CCLOG_INFO, stream << "found locked-in-loop opret" << " addr=" << coinaddr << " isEqualAddr=" << isEqualAddr << std::endl);
            return isEqualAddr ? 1 : 0;
        }
    }
    
    LOGSTREAMFN("marmara", CCLOG_DEBUG2, stream << "incorrect stake tx vout or opret, returning 0, txid=" << staketx.GetHash().ToString() << " stake tx hex=" << HexStr(E_MARSHAL(ss << staketx)) << " inOpret=" << inOpret.ToString() << std::endl);
    return 0;
}

// enumerates mypk activated cc vouts
// calls a callback allowing to do something with the utxos (add to staking utxo array)
// TODO: maybe better to use AddMarmaraInputs with a callback for unification...
template <class T>
static void EnumMyActivated(T func)
{
    struct CCcontract_info *cp, C;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > activatedOutputs;

    cp = CCinit(&C, EVAL_MARMARA);
    CPubKey mypk = pubkey2pk(Mypubkey());
    CPubKey Marmarapk = GetUnspendable(cp, NULL);

    char activatedaddr[KOMODO_ADDRESS_BUFSIZE];
    GetCCaddress1of2(cp, activatedaddr, Marmarapk, mypk);

    // add activated coins for mypk:
    SetCCunspents(activatedOutputs, activatedaddr, true);

    // add my activated coins:
    LOGSTREAMFN("marmara", CCLOG_DEBUG3, stream  << " check activatedaddr" << activatedaddr << std::endl);
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = activatedOutputs.begin(); it != activatedOutputs.end(); it++)
    {
        CTransaction tx; uint256 hashBlock;
        CBlockIndex *pindex;

        uint256 txid = it->first.txhash;
        int32_t nvout = (int32_t)it->first.index;
        CAmount nValue;

        if ((nValue = it->second.satoshis) < COIN)   // skip small values
            continue;

        LOGSTREAMFN("marmara", CCLOG_DEBUG3, stream  << " check tx on activatedaddr txid=" << txid.GetHex() << " vout=" << nvout << std::endl);

        if (GetTransaction(txid, tx, hashBlock, true) && (pindex = komodo_getblockindex(hashBlock)) != 0 && myIsutxo_spentinmempool(ignoretxid, ignorevin, txid, nvout) == 0)
        {
            char utxoaddr[KOMODO_ADDRESS_BUFSIZE] = "";

            Getscriptaddress(utxoaddr, tx.vout[nvout].scriptPubKey);
            if (strcmp(activatedaddr, utxoaddr) == 0)  // check if real vout address matches index address (as another key could be used in the addressindex)
            {
                CScript opret;
                CPubKey senderpk;
                if (CheckEitherOpRet(IsActivatedOpret, tx, nvout, opret, senderpk))
                {
                    // call callback function:
                    func(activatedaddr, tx, nvout, pindex);
                    LOGSTREAMFN("marmara", CCLOG_DEBUG3, stream  << " found my activated 1of2 addr txid=" << txid.GetHex() << " vout=" << nvout << std::endl);
                }
                else
                    LOGSTREAMFN("marmara", CCLOG_ERROR, stream  << " skipped activated 1of2 addr txid=" << txid.GetHex() << " vout=" << nvout << " cant decode opret or not mypk" << std::endl);
            }
            else
                LOGSTREAMFN("marmara", CCLOG_ERROR, stream  << " skipped activated 1of2 addr txid=" << txid.GetHex() << " vout=" << nvout << " uxto addr not matched index" << std::endl);
        }
    }
}

// enumerates mypk locked in loop cc vouts
// calls a callback allowing to do something with the utxos (add to staking utxo array)
// TODO: maybe better to use AddMarmaraInputs with a callback for unification...
template <class T>
static void EnumMyLockedInLoop(T func)
{
    char markeraddr[KOMODO_ADDRESS_BUFSIZE];
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > markerOutputs;

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_MARMARA);
    CPubKey mypk = pubkey2pk(Mypubkey());
    CPubKey Marmarapk = GetUnspendable(cp, NULL);

    GetCCaddress(cp, markeraddr, Marmarapk);
    SetCCunspents(markerOutputs, markeraddr, true);

    // enum all createtxids:
    LOGSTREAMFN("marmara", CCLOG_DEBUG3, stream  << " check on markeraddr=" << markeraddr << std::endl);
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = markerOutputs.begin(); it != markerOutputs.end(); it++)
    {
        CTransaction isssuancetx;
        uint256 hashBlock;
        uint256 txid = it->first.txhash;
        int32_t nvout = (int32_t)it->first.index;

        LOGSTREAMFN("marmara", CCLOG_DEBUG3, stream  << " checking tx on markeraddr txid=" << txid.GetHex() << " vout=" << nvout << std::endl);
        if (nvout == MARMARA_MARKER_VOUT && GetTransaction(txid, isssuancetx, hashBlock, true))  // TODO: check if non-locking version better, was GetTransaction(txid, isssuancetx, hashBlock, true)
        {
            if (!isssuancetx.IsCoinBase() && isssuancetx.vout.size() > 2 && isssuancetx.vout.back().nValue == 0)
            {
                struct CreditLoopOpret loopData;

                if (MarmaraDecodeLoopOpret(isssuancetx.vout.back().scriptPubKey, loopData) == 'I')
                {
                    char loopaddr[KOMODO_ADDRESS_BUFSIZE];
                    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > loopOutputs;
                    char txidaddr[KOMODO_ADDRESS_BUFSIZE];
                    CPubKey createtxidPk = CCtxidaddr(txidaddr, loopData.createtxid);

                    GetCCaddress1of2(cp, loopaddr, Marmarapk, createtxidPk);
                    SetCCunspents(loopOutputs, loopaddr, true);

                    // enum all locked-in-loop addresses:
                    LOGSTREAMFN("marmara", CCLOG_DEBUG3, stream  << " checking on loopaddr=" << loopaddr << std::endl);
                    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = loopOutputs.begin(); it != loopOutputs.end(); it++)
                    {
                        CTransaction looptx;
                        uint256 hashBlock;
                        CBlockIndex *pindex;
                        uint256 txid = it->first.txhash;
                        int32_t nvout = (int32_t)it->first.index;

                        LOGSTREAMFN("marmara", CCLOG_DEBUG3, stream  << " checking tx on loopaddr txid=" << txid.GetHex() << " vout=" << nvout << std::endl);

                        if (GetTransaction(txid, looptx, hashBlock, true) && (pindex = komodo_getblockindex(hashBlock)) != 0 && myIsutxo_spentinmempool(ignoretxid, ignorevin, txid, nvout) == 0)  // TODO: change to the non-locking version
                        {
                            /* lock-in-loop cant be mined */                   /* now it could be cc opret, not necessary OP_RETURN vout in the back */
                            if (!looptx.IsCoinBase() && looptx.vout.size() > 0 /* && looptx.vout.back().nValue == 0 */)  
                            {
                                char utxoaddr[KOMODO_ADDRESS_BUFSIZE] = "";

                                Getscriptaddress(utxoaddr, looptx.vout[nvout].scriptPubKey);
                                if (strcmp(loopaddr, utxoaddr) == 0)  // check if real vout address matches index address (as another key could be used in the addressindex)
                                {
                                    CScript opret;
                                    CPubKey senderpk;

                                    if (CheckEitherOpRet(IsLockInLoopOpret, looptx, nvout, opret, senderpk))
                                    {
                                        if (mypk == senderpk)   // check mypk in opret
                                        {
                                            // call callback func:
                                            func(loopaddr, looptx, nvout, pindex);
                                            LOGSTREAMFN("marmara", CCLOG_DEBUG3, stream << " found my lock-in-loop 1of2 addr txid=" << txid.GetHex() << " vout=" << nvout << std::endl);
                                        }
                                        else
                                            LOGSTREAMFN("marmara", CCLOG_DEBUG2, stream << " skipped lock-in-loop 1of2 addr txid=" << txid.GetHex() << " vout=" << nvout << " not mypk" << std::endl);
                                    }
                                    else
                                        LOGSTREAMFN("marmara", CCLOG_ERROR, stream << " skipped lock-in-loop 1of2 addr txid=" << txid.GetHex() << " vout=" << nvout << " cant decode opret" << std::endl);
                                }
                                else
                                    LOGSTREAMFN("marmara", CCLOG_ERROR, stream  << " skipped lock-in-loop 1of2 addr txid=" << txid.GetHex() << " vout=" << nvout << " uxto addr not matched index" << std::endl);
                            }
                        }
                    }
                }
            }
        }
        else
            LOGSTREAMFN("marmara", CCLOG_ERROR, stream << " error getting tx=" << txid.GetHex() << std::endl);
    }
}


// add marmara special UTXO from activated and lock-in-loop addresses for staking
// called from PoS code
struct komodo_staking *MarmaraGetStakingUtxos(struct komodo_staking *array, int32_t *numkp, int32_t *maxkp, uint8_t *hashbuf)
{
    const char *logFName = __func__;

    // add activated utxos for mypk:
    //std::cerr  << " entered" << std::endl;
    EnumMyActivated([&](char *activatedaddr, const CTransaction & tx, int32_t nvout, CBlockIndex *pindex) 
    {
        array = komodo_addutxo(array, numkp, maxkp, (uint32_t)pindex->nTime, (uint64_t)tx.vout[nvout].nValue, tx.GetHash(), nvout, activatedaddr, hashbuf, tx.vout[nvout].scriptPubKey);
        LOGSTREAM("marmara", CCLOG_DEBUG3, stream << logFName << " added uxto for staking activated 1of2 addr txid=" << tx.GetHash().GetHex() << " vout=" << nvout << std::endl);
    });

    // add lock-in-loops utxos for mypk:
    EnumMyLockedInLoop([&](char *loopaddr, const CTransaction & tx, int32_t nvout, CBlockIndex *pindex)
    {
        array = komodo_addutxo(array, numkp, maxkp, (uint32_t)pindex->nTime, (uint64_t)tx.vout[nvout].nValue, tx.GetHash(), nvout, loopaddr, hashbuf, tx.vout[nvout].scriptPubKey);
        LOGSTREAM("marmara", CCLOG_DEBUG3, stream << logFName << " added uxto for staking lock-in-loop 1of2addr txid=" << tx.GetHash().GetHex() << " vout=" << nvout << std::endl);
    });
   
    return array;
}

// returns stake preferences for activated and locked vouts
int32_t MarmaraGetStakeMultiplier(const CTransaction & tx, int32_t nvout)
{
    CScript opret;
    CPubKey mypk = pubkey2pk(Mypubkey());
    CPubKey opretpk;

    if (nvout > 0 && nvout < tx.vout.size()) // check boundary
    {
        if (CheckEitherOpRet(IsLockInLoopOpret, tx, nvout, opret, opretpk) && mypk == opretpk)   // check if opret is lock-in-loop and cc vout is mypk
        {
            if (tx.vout[nvout].scriptPubKey.IsPayToCryptoCondition()) 
            {
                uint8_t funcid = 0;
                struct CreditLoopOpret loopData;

                if (MarmaraDecodeLoopOpret(tx.vout.back().scriptPubKey, loopData) != 0)     
                {
                    struct CCcontract_info *cp, C;
                    cp = CCinit(&C, EVAL_MARMARA);
                    CPubKey Marmarapk = GetUnspendable(cp, NULL);

                    char txidaddr[KOMODO_ADDRESS_BUFSIZE];
                    char lockInLoop1of2addr[KOMODO_ADDRESS_BUFSIZE];
                    char ccvoutaddr[KOMODO_ADDRESS_BUFSIZE];
                    CPubKey createtxidPk = CCtxidaddr(txidaddr, loopData.createtxid);
                    GetCCaddress1of2(cp, lockInLoop1of2addr, Marmarapk, createtxidPk);
                    Getscriptaddress(ccvoutaddr, tx.vout[nvout].scriptPubKey);

                    if (strcmp(lockInLoop1of2addr, ccvoutaddr) == 0)  // check vout address is lock-in-loop address
                    {
                        LOGSTREAMFN("marmara", CCLOG_DEBUG1, stream  << " utxo picked for stake x3 as lock-in-loop" << " txid=" << tx.GetHash().GetHex() << " nvout=" << nvout << std::endl);
                        return 3;  // staked 3 times for lock-in-loop
                    }
                }
            }
        }

        if (CheckEitherOpRet(IsActivatedOpret, tx, nvout, opret, opretpk))   // check if this is activated opret 
        {
            if (tx.vout[nvout].scriptPubKey.IsPayToCryptoCondition())    
            {    
                struct CCcontract_info *cp, C;
                cp = CCinit(&C, EVAL_MARMARA);
                CPubKey Marmarapk = GetUnspendable(cp, NULL);

                char activated1of2addr[KOMODO_ADDRESS_BUFSIZE];
                char voutaddr[KOMODO_ADDRESS_BUFSIZE];
                GetCCaddress1of2(cp, activated1of2addr, Marmarapk, mypk);
                Getscriptaddress(voutaddr, tx.vout[nvout].scriptPubKey);

                if (strcmp(activated1of2addr, voutaddr) == 0)   // check vout address is my activated address
                {
                    LOGSTREAMFN("marmara", CCLOG_DEBUG1, stream  << " utxo picked for stake x1 as activated" << " txid=" << tx.GetHash().GetHex() << " nvout=" << nvout << std::endl);
                    return 1;  // staked 1 times for activated
                }
            }
        }
    }
    return 1;
}


// consensus code:

bool MarmaraValidate(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn)
{   
    vscript_t vopret; CTransaction vinTx; uint256 hashBlock;  int32_t numvins, numvouts, i, ht, unlockht, vht, vunlockht; uint8_t funcid, vfuncid, *script; CPubKey pk, vpk;
    if (ASSETCHAINS_MARMARA == 0)
        return eval->Invalid("-ac_marmara must be set for marmara CC");
    numvins = tx.vin.size();
    numvouts = tx.vout.size();
    if (numvouts < 1)
        return eval->Invalid("no vouts");
    else if (tx.vout.size() >= 2)
    {
        GetOpReturnData(tx.vout[tx.vout.size() - 1].scriptPubKey, vopret);
        script = (uint8_t *)vopret.data();
        if (vopret.size() < 2 || script[0] != EVAL_MARMARA)
            return eval->Invalid("no opreturn");
        funcid = script[1];
        if (funcid == 'P')
        {
            funcid = MarmaraDecodeCoinbaseOpret(tx.vout[tx.vout.size() - 1].scriptPubKey, pk, ht, unlockht);
            for (i = 0; i < numvins; i++)
            {
                if ((*cp->ismyvin)(tx.vin[i].scriptSig) != 0)
                {
                    if (eval->GetTxUnconfirmed(tx.vin[i].prevout.hash, vinTx, hashBlock) == 0)
                        return eval->Invalid("cant find vinTx");
                    else
                    {
                        if (vinTx.IsCoinBase() == 0)
                            return eval->Invalid("noncoinbase input");
                        else if (vinTx.vout.size() != 2)
                            return eval->Invalid("coinbase doesnt have 2 vouts");
                        vfuncid = MarmaraDecodeCoinbaseOpret(vinTx.vout[1].scriptPubKey, vpk, vht, vunlockht);
                        if (vfuncid != 'C' || vpk != pk || vunlockht != unlockht)
                            return eval->Invalid("mismatched opreturn");
                    }
                }
            }
            return(true);
        }
        else if (funcid == 'L') // lock -> lock funds with a unlockht
        {
            return(true);
        }
        else if (funcid == 'R') // receive -> agree to receive 'I' from pk, amount, currency, dueht
        {
            return(true);
        }
        else if (funcid == 'I') // issue -> issue currency to pk with due date height
        {
            return(true);
        }
        else if (funcid == 'T') // transfer -> given 'R' transfer 'I' or 'T' to the pk of 'R'
        {
            return(true);
        }
        else if (funcid == 'S') // settlement -> automatically spend issuers locked funds, given 'I'
        {
            return(true);
        }
        else if (funcid == 'D') // insufficient settlement
        {
            return(true);
        }
        else if (funcid == 'C') // coinbase
        {
            return(true);
        }
        else if (funcid == 'K') // lock-in-loop
        {
            return(true);
        }
        // staking only for locked utxo
    }
    LOGSTREAMFN("marmara", CCLOG_ERROR, stream << " validation error for txid=" << tx.GetHash().GetHex() << " bad funcid=" << (char)(funcid ? funcid : ' ') << std::endl);
    return eval->Invalid("fall through error");
}
// end of consensus code

// helper functions for rpc calls in rpcwallet.cpp

// add mined coins
int64_t AddMarmaraCoinbases(struct CCcontract_info *cp, CMutableTransaction &mtx, int32_t firstheight, CPubKey poolpk, int32_t maxinputs)
{
    char coinaddr[KOMODO_ADDRESS_BUFSIZE]; 
    CPubKey Marmarapk, pk; 
    int64_t nValue, totalinputs = 0; 
    uint256 txid, hashBlock; 
    CTransaction vintx; 
    int32_t unlockht, ht, vout, unlocks, n = 0;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;

    Marmarapk = GetUnspendable(cp, 0);
    GetCCaddress1of2(cp, coinaddr, Marmarapk, poolpk);
    SetCCunspents(unspentOutputs, coinaddr, true);
    unlocks = MarmaraUnlockht(firstheight);

    LOGSTREAMFN("marmara", CCLOG_DEBUG2, stream  << " check coinaddr=" << coinaddr << std::endl);
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = unspentOutputs.begin(); it != unspentOutputs.end(); it++)
    {
        txid = it->first.txhash;
        vout = (int32_t)it->first.index;
        LOGSTREAMFN("marmara", CCLOG_DEBUG2, stream  << " txid=" << txid.GetHex() << " vout=" << vout << std::endl);
        if (myGetTransaction(txid,vintx,hashBlock) != 0)
        {
            if (vintx.IsCoinBase() != 0 && vintx.vout.size() == 2 && vintx.vout[1].nValue == 0)
            {
                if (MarmaraDecodeCoinbaseOpret(vintx.vout[1].scriptPubKey, pk, ht, unlockht) == 'C' && unlockht == unlocks && pk == poolpk && ht >= firstheight)
                {
                    if ((nValue = vintx.vout[vout].nValue) > 0 && myIsutxo_spentinmempool(ignoretxid, ignorevin, txid, vout) == 0)
                    {
                        if (maxinputs != 0)
                            mtx.vin.push_back(CTxIn(txid, vout, CScript()));
                        nValue = it->second.satoshis;
                        totalinputs += nValue;
                        n++;
                        if (maxinputs > 0 && n >= maxinputs)
                            break;
                    } 
                    else
                        LOGSTREAMFN("marmara", CCLOG_ERROR, stream << "tx in mempool or vout not positive, nValue=" << nValue << std::endl);
                } 
                else 
                    LOGSTREAMFN("marmara", CCLOG_ERROR, stream  << "decode error unlockht=" << unlockht << " vs unlocks=" << unlocks << " is-pool-pk=" << (pk == poolpk) << std::endl);
            }
            else
                LOGSTREAMFN("marmara", CCLOG_ERROR, stream  << "not coinbase" << std::endl);
        }
        else
            LOGSTREAMFN("marmara", CCLOG_ERROR, stream  << "error getting tx=" << txid.GetHex() << std::endl);
    }
    return(totalinputs);
}

// checks if opret for activated coins, returns pk from opret
static bool IsActivatedOpret(const CScript &spk, CPubKey &pk) 
{ 
    uint8_t funcid;
    int32_t ht, unlockht;
   
    return (funcid = MarmaraDecodeCoinbaseOpret(spk, pk, ht, unlockht)) == 'C' || funcid == 'P' || funcid == 'L';
}

// checks if opret for lock-in-loop coins, returns pk from opret
static bool IsLockInLoopOpret(const CScript &spk, CPubKey &pk)
{
    struct CreditLoopOpret loopData;

    uint8_t funcid = MarmaraDecodeLoopOpret(spk, loopData);
    if (funcid != 0) {
        pk = loopData.pk;
        return true;
    }
    return false;
}

// checks opret by calling CheckOpretFunc for two cases:
// 1) opret in cc vout data is checked first and considered primary
// 2) opret in the last vout is checked second and considered secondary
// returns the opret and sender pubkey from the opret
static bool CheckEitherOpRet(bool(*CheckOpretFunc)(const CScript &, CPubKey &), const CTransaction &tx, int32_t nvout, CScript &opretOut, CPubKey & senderpk)
{
    CScript opret, dummy;
    std::vector< vscript_t > vParams;
    bool isccopret = false, opretok = false;

    // first check cc opret
    tx.vout[nvout].scriptPubKey.IsPayToCryptoCondition(&dummy, vParams);
    if (vParams.size() > 0)     {
        COptCCParams p = COptCCParams(vParams[0]);
        if (p.vData.size() > 0) {
            opret << OP_RETURN << p.vData[0]; // reconstruct opret for CheckOpretFunc function
            LOGSTREAMFN("marmara", CCLOG_DEBUG3, stream  << " ccopret=" << opret.ToString() << std::endl);
            if (CheckOpretFunc(opret, senderpk)) {
                isccopret = true;
                opretok = true;
                opretOut = opret;
            }
        }
    }

    // then check opret  in the last vout:
    if (!opretok) {  // right opret not found in cc vout then check opret in the back of vouts
        if (nvout < tx.vout.size()) {   // there might be opret in the back
            opret = tx.vout.back().scriptPubKey;
            if (CheckOpretFunc(opret, senderpk)) {
                isccopret = false;
                opretok = true;
                opretOut = opret;
            }
        }
    }

    // print opret evalcode and funcid for debug logging:
    vscript_t vprintopret;
    uint8_t funcid = 0, evalcode = 0;
    if (GetOpReturnData(opret, vprintopret) && vprintopret.size() >= 2) {
        evalcode = vprintopret.begin()[0];
        funcid = vprintopret.begin()[1];
    }
    LOGSTREAMFN("marmara", CCLOG_DEBUG3, stream  << " opret eval=" << (int)evalcode << " funcid=" << (char)(funcid ? funcid : ' ') << " isccopret=" << isccopret << std::endl);
    
    return opretok;
}

#define LL(s, l, op) LOGSTREAMFN(s, l, op)
// add activated or locked-in-loop coins from 1of2 address 
// for lock-in-loop mypk not checked, so all locked-in-loop utxos for an address are added:
int64_t AddMarmarainputs(bool (*CheckOpretFunc)(const CScript &, CPubKey &), CMutableTransaction &mtx, std::vector<CPubKey> &pubkeys, char *unspentaddr, int64_t total, int32_t maxinputs)
{
    int64_t threshold, nValue, totalinputs = 0; 
    int32_t n = 0;
    std::vector<int64_t> vals;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;

    SetCCunspents(unspentOutputs, unspentaddr, true);
    if (maxinputs > CC_MAXVINS)
        maxinputs = CC_MAXVINS;
    if (maxinputs > 0)
        threshold = total / maxinputs;
    else 
        threshold = total;

    if (CheckOpretFunc == NULL)  // no function to check opret
        return -1;

    LOGSTREAMFN("marmara", CCLOG_DEBUG2, stream  << " adding from addr=" << unspentaddr << " total=" << total << std::endl);
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = unspentOutputs.begin(); it != unspentOutputs.end(); it++)
    {
        uint256 txid = it->first.txhash;
        int32_t nvout = (int32_t)it->first.index;
        uint256 hashBlock;
        CTransaction tx;

        if (it->second.satoshis < threshold)
            continue;

        // check if vin might be already added to mtx:
        if (std::find_if(mtx.vin.begin(), mtx.vin.end(), [&](CTxIn v) {return (v.prevout.hash == txid && v.prevout.n == nvout); }) != mtx.vin.end())
            continue;

        if (myGetTransaction(txid, tx, hashBlock) != 0 && tx.vout.size() > 0 && 
            tx.vout[nvout].scriptPubKey.IsPayToCryptoCondition() != 0 && myIsutxo_spentinmempool(ignoretxid, ignorevin, txid, nvout) == 0)
        {
            CPubKey senderpk;
            CScript opret;
            std::vector< vscript_t > vParams;
            bool isccopret = false, opretok = false;

            // this check considers 2 cases:
            // first if opret is in the cc vout data
            // second if opret is in the last vout
            if (CheckEitherOpRet(CheckOpretFunc, tx, nvout, opret, senderpk))
            {
                char utxoaddr[KOMODO_ADDRESS_BUFSIZE];

                Getscriptaddress(utxoaddr, tx.vout[nvout].scriptPubKey);
                if (strcmp(unspentaddr, utxoaddr) == 0)  // check if the real vout address matches the index address (as another key could be used in the addressindex)
                {
                    LOGSTREAMFN("marmara", CCLOG_DEBUG2, stream  << " found good vintx for addr=" << unspentaddr << " txid=" << txid.GetHex() << " nvout=" << nvout << " satoshis=" << it->second.satoshis << " isccopret=" << isccopret << std::endl);

                    if (total != 0 && maxinputs != 0)
                    {
                        mtx.vin.push_back(CTxIn(txid, nvout, CScript()));
                        pubkeys.push_back(senderpk);
                    }
                    totalinputs += it->second.satoshis;
                    vals.push_back(it->second.satoshis);
                    n++;
                    if (maxinputs != 0 && total == 0)  
                        continue;
                    if ((total > 0 && totalinputs >= total) || (maxinputs > 0 && n >= maxinputs))
                        break;
                }
            }
            else
                LOGSTREAMFN("marmara", CCLOG_ERROR, stream  << " addr=" << unspentaddr << " txid=" << txid.GetHex() << " cant check opret" << std::endl);
        }
    }
    if (maxinputs != 0 && total == 0)
    {
        std::sort(vals.begin(), vals.end());
        totalinputs = 0;
        for (int32_t i = 0; i < maxinputs && i < vals.size(); i++)
            totalinputs += vals[i];
    }

    LOGSTREAMFN("marmara", CCLOG_DEBUG1, stream  << " for addr=" << unspentaddr << " found total=" << totalinputs << std::endl);
    return(totalinputs);
}

// lock the amount on the specified block height
UniValue MarmaraLock(int64_t txfee, int64_t amount)
{
    CMutableTransaction tmpmtx, mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    UniValue result(UniValue::VOBJ);
    struct CCcontract_info *cp, C;
    CPubKey Marmarapk, mypk, pk;
    //int32_t unlockht, /*refunlockht,*/ nvout, ht, numvouts;
    int64_t nValue, val, inputsum = 0, threshold, remains, change = 0;
    std::string rawtx, errorstr;
    char mynormaladdr[KOMODO_ADDRESS_BUFSIZE], activated1of2addr[KOMODO_ADDRESS_BUFSIZE];
    uint256 txid, hashBlock;
    CTransaction tx;
    uint8_t funcid;

    if (txfee == 0)
        txfee = 10000;

    int32_t height = komodo_nextheight();
    // as opret creation function MarmaraCoinbaseOpret creates opret only for even blocks - adjust this base height to even value
    if ((height & 1) != 0)
         height++;

    cp = CCinit(&C, EVAL_MARMARA);
    mypk = pubkey2pk(Mypubkey());
    Marmarapk = GetUnspendable(cp, 0);

    Getscriptaddress(mynormaladdr, CScript() << ParseHex(HexStr(mypk)) << OP_CHECKSIG);
    if ((val = CCaddress_balance(mynormaladdr, 0)) < amount) // if not enough funds in the wallet
        val -= 2 * txfee;    // dont take all, should al least 1 txfee remained 
    else
        val = amount;
    if (val > txfee)
        inputsum = AddNormalinputs2(mtx, val + txfee, CC_MAXVINS / 2);  //added '+txfee' because if 'inputsum' exactly was equal to 'val' we'd exit from insufficient funds 
    //fprintf(stderr,"%s added normal inputs=%.8f required val+txfee=%.8f\n", logFuncName, (double)inputsum/COIN,(double)(val+txfee)/COIN);

    // lock the amount on 1of2 address:
    mtx.vout.push_back(MakeCC1of2vout(EVAL_MARMARA, amount, Marmarapk, mypk));

    if (inputsum < amount + txfee)  // if not enough normal inputs for collateral
    {
        //refunlockht = MarmaraUnlockht(height);  // randomized 

        result.push_back(Pair("normalfunds", ValueFromAmount(inputsum)));
        result.push_back(Pair("height", height));
        //result.push_back(Pair("unlockht", refunlockht));

        // fund remainder to add:
        remains = (amount + txfee) - inputsum;

        std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;
        GetCCaddress1of2(cp, activated1of2addr, Marmarapk, mypk);
        SetCCunspents(unspentOutputs, activated1of2addr, true);
        threshold = remains / (MARMARA_VINS + 1);
        uint8_t mypriv[32];
        Myprivkey(mypriv);
        CCaddr1of2set(cp, Marmarapk, mypk, mypriv, activated1of2addr);

        // try to add collateral remainder from the activated  fund (and re-lock it):
        /* we cannot do this any more as activated funds are locked to the max height
           we need first unlock them to normal to move to activated again:
        for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = unspentOutputs.begin(); it != unspentOutputs.end(); it++)
        {
            txid = it->first.txhash;
            nvout = (int32_t)it->first.index;
            if ((nValue = it->second.satoshis) < threshold)
                continue;
            if (myGetTransaction(txid, tx, hashBlock) != 0 && (numvouts = tx.vout.size()) > 0 && nvout < numvouts && tx.vout[nvout].scriptPubKey.IsPayToCryptoCondition() != 0 && myIsutxo_spentinmempool(ignoretxid, ignorevin, txid, nvout) == 0)
            {
                if ((funcid = DecodeMarmaraCoinbaseOpRet(tx.vout[numvouts - 1].scriptPubKey, pk, ht, unlockht)) == 'C' || funcid == 'P' || funcid == 'L')
                {
                    if (unlockht < refunlockht)  // if allowed to unlock already
                    {
                        mtx.vin.push_back(CTxIn(txid, nvout, CScript()));
                        //fprintf(stderr,"merge CC vout %s/v%d %.8f unlockht.%d < ref.%d\n",txid.GetHex().c_str(),vout,(double)nValue/COIN,unlockht,refunlockht);
                        inputsum += nValue;
                        remains -= nValue;
                        if (inputsum >= amount + txfee)
                        {
                            //fprintf(stderr,"inputsum %.8f >= amount %.8f, update amount\n",(double)inputsum/COIN,(double)amount/COIN);
                            amount = inputsum - txfee;
                            break;
                        }
                    }
                }
                else    
                    std::cerr , " incorrect funcid in tx from locked fund" << std::endl;
            }
            else  
                std::cerr  << " could not load or incorrect tx from locked fund" << std::endl;
        }   */

        memset(mypriv,0,sizeof(mypriv));
    }
    if (inputsum >= amount + txfee)
    {
        if (inputsum > amount + txfee)
        {
            change = (inputsum - amount);
            mtx.vout.push_back(CTxOut(change, CScript() << ParseHex(HexStr(mypk)) << OP_CHECKSIG));
        }
        rawtx = FinalizeCCTx(0, cp, mtx, mypk, txfee, MarmaraCoinbaseOpret('L', height, mypk));
        if (rawtx.size() == 0)
        {
            errorstr = (char *)"couldnt finalize CCtx";
        }
        else
        {
            result.push_back(Pair("result", (char *)"success"));
            result.push_back(Pair("hex", rawtx));
            return(result);
        }
    }
    else
        errorstr = (char *)"insufficient funds";
    result.push_back(Pair("result", (char *)"error"));
    result.push_back(Pair("error", errorstr));
    return(result);
}

// finalize and sign stake tx on activated or lock-in-loop 1of2 addr
// note: utxosig bufsize = 512
int32_t MarmaraSignature(uint8_t *utxosig, CMutableTransaction &mtx)
{
    uint256 txid, hashBlock; 
    CTransaction vintx; 
    int64_t txfee = 10000;

    int32_t vout = mtx.vin[0].prevout.n;
    if (myGetTransaction(mtx.vin[0].prevout.hash, tx, hashBlock) != 0 && tx.vout.size() > 1 && vout < tx.vout.size())
    {
        /*
        std::vector<CPubKey> pubkeys;
        struct CCcontract_info *cp, C;

        cp = CCinit(&C, EVAL_MARMARA);
        CPubKey mypk = pubkey2pk(Mypubkey());
        uint8_t marmarapriv[32];
        CPubKey Marmarapk = GetUnspendable(cp, marmarapriv);
        pubkeys.push_back(mypk);

        uint256 createtxid;
        CPubKey issuerpk;
        int64_t amount;
        int32_t matures;
        std::string currency;

        CC *probeCond = NULL;
        // check utxo if it is activated or locked-in-loop
        if (MarmaraDecodeLoopOpret(vintx.vout.back().scriptPubKey, createtxid, issuerpk, amount, matures, currency) != 0)   // is this locked-in-loop utxo?
        {
            char  txidaddr[KOMODO_ADDRESS_BUFSIZE];
            CPubKey createtxidPk = CCtxidaddr(txidaddr, createtxid);
            probeCond = MakeCCcond1of2(EVAL_MARMARA, Marmarapk, createtxidPk);
        }
        else
        {
            probeCond = MakeCCcond1of2(EVAL_MARMARA, Marmarapk, mypk);
        }*/

        CScript vintxOpret;
        struct CCcontract_info *cp, C;
        cp = CCinit(&C, EVAL_MARMARA);
        uint8_t marmarapriv[32];
        CPubKey Marmarapk = GetUnspendable(cp, marmarapriv);

        CPubKey mypk = pubkey2pk(Mypubkey());
        CPubKey senderpk;
        CC *probeCond = NULL;

        if (CheckEitherOpRet(IsActivatedOpret, vintx, mtx.vin[0].prevout.n, vintxOpret, senderpk))
        {
            //int32_t height, unlockht;
            //funcid = DecodeMarmaraCoinbaseOpRet(opret, senderpk, height, unlockht);

            LOGSTREAMFN("marmara", CCLOG_INFO, stream << "found activated opret in vintx" << std::endl);
            probeCond = MakeCCcond1of2(EVAL_MARMARA, Marmarapk, mypk);
        }
        else if (CheckEitherOpRet(IsLockInLoopOpret, vintx, mtx.vin[0].prevout.n, vintxOpret, senderpk))
        {
            struct CreditLoopOpret loopData;

            MarmaraDecodeLoopOpret(vintxOpret, loopData);

            char txidaddr[KOMODO_ADDRESS_BUFSIZE];
            CPubKey createtxidPk = CCtxidaddr(txidaddr, loopData.createtxid);

            LOGSTREAMFN("marmara", CCLOG_INFO, stream << "found locked-in-loop opret in vintx" << std::endl);
            probeCond = MakeCCcond1of2(EVAL_MARMARA, Marmarapk, createtxidPk);
        }

        CCAddVintxCond(cp, probeCond, marmarapriv); //add probe condition to sign vintx 1of2 utxo

        // note: opreturn for stake tx is taken from the staking utxo (ccvout or back):
        std::string rawtx = FinalizeCCTx(0, cp, mtx, mypk, txfee, vintxOpret);  
        cc_free(probeCond);

        if (rawtx.size() > 0)
        {
            int32_t siglen = mtx.vin[0].scriptSig.size();
            uint8_t *scriptptr = &mtx.vin[0].scriptSig[0];

            if (siglen > 512) {   // check sig buffer limit
                LOGSTREAMFN("marmara", CCLOG_ERROR, stream << "scriptSig length is more than utxosig bufsize, truncated! siglen=" << siglen << std::endl);
                siglen = 512;
            }

            std::ostringstream debstream;
            for (int32_t i = 0; i < siglen; i++)
            {
                utxosig[i] = scriptptr[i];
                debstream << std::hex << (int)scriptptr[i];
            }
            std::string strScriptSig = debstream.str();

            LOGSTREAMFN("marmara", CCLOG_DEBUG2, stream << "scriptSig=" << strScriptSig << " got signed rawtx=" << rawtx << " siglen=" << siglen << std::endl);
            return(siglen);
        }
        else
            LOGSTREAMFN("marmara", CCLOG_ERROR, stream << "cannot sign activated staked tx, bad mtx=" << HexStr(E_MARSHAL(ss << mtx)) <<  std::endl);
    }
    else 
        LOGSTREAMFN("marmara", CCLOG_ERROR, stream << "cannot get vintx for staked tx" << std::endl);
    return(0);
}

// jl777: decide on what unlockht settlement change should have -> from utxo making change

UniValue MarmaraSettlement(int64_t txfee, uint256 refbatontxid, CTransaction &settlementTx)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    UniValue result(UniValue::VOBJ);
    std::vector<uint256> creditloop;
    uint256 batontxid;
    int32_t numerrs = 0, numDebtors;
    int64_t inputsum;
    std::string rawtx;
    char loop1of2addr[KOMODO_ADDRESS_BUFSIZE], myCCaddr[KOMODO_ADDRESS_BUFSIZE], destaddr[KOMODO_ADDRESS_BUFSIZE], batonCCaddr[KOMODO_ADDRESS_BUFSIZE], txidaddr[KOMODO_ADDRESS_BUFSIZE];
    struct CCcontract_info *cp, C;

    if (txfee == 0)
        txfee = 10000;

    cp = CCinit(&C, EVAL_MARMARA);
    CPubKey minerpk = pubkey2pk(Mypubkey());
    uint8_t marmarapriv[32];
    CPubKey Marmarapk = GetUnspendable(cp, marmarapriv);
    
    int64_t change = 0;
    int32_t height = chainActive.LastTip()->GetHeight();
    if ((numDebtors = MarmaraGetbatontxid(creditloop, batontxid, refbatontxid)) > 0)
    {
        CTransaction batontx;
        uint256 hashBlock;
        struct CreditLoopOpret loopData;

        if( MarmaraGetLoopCreateData(creditloop[0], loopData) == 0 )
        {

            if (GetTransaction(batontxid, batontx, hashBlock, true) && !hashBlock.IsNull() && batontx.vout.size() > 1)
            {
                CPubKey currentpk;
                uint8_t funcid;

                if ((funcid = MarmaraDecodeLoopOpret(batontx.vout.back().scriptPubKey, loopData)) != 0)
                {
                    if (loopData.createtxid != creditloop[0])
                    {
                        result.push_back(Pair("result", (char *)"error"));
                        result.push_back(Pair("error", (char *)"invalid refcreatetxid, should be set to creditloop[0]")); //TODO: note change
                        return(result);
                    }
                    else if (chainActive.LastTip()->GetHeight() < loopData.matures)
                    {
                        fprintf(stderr, "doesnt mature for another %d blocks\n", loopData.matures - chainActive.LastTip()->GetHeight());
                        result.push_back(Pair("result", (char *)"error"));
                        result.push_back(Pair("error", (char *)"cant settle immature creditloop"));
                        return(result);
                    }
                    else if ((loopData.matures & 1) == 0)
                    {
                        result.push_back(Pair("result", (char *)"error"));
                        result.push_back(Pair("error", (char *)"cant automatic settle even maturity heights"));
                        return(result);
                    }
                    else if (numDebtors < 1)
                    {
                        result.push_back(Pair("result", (char *)"error"));
                        result.push_back(Pair("error", (char *)"creditloop too short"));
                        return(result);
                    }
                    // remaining = refamount;
                    GetCCaddress(cp, myCCaddr, Mypubkey());
                    Getscriptaddress(batonCCaddr, batontx.vout[0].scriptPubKey);

                    // allow any miner to settle, do not check mypk:
                    //if (strcmp(myCCaddr, batonCCaddr) == 0) // if mypk user owns the baton
                    {
                        std::vector<CPubKey> pubkeys;

                        mtx.vin.push_back(CTxIn(numDebtors == 1 ? batontxid : creditloop[1], MARMARA_MARKER_VOUT, CScript())); // spend issuance marker - close the loop

                        // add tx fee from mypubkey
                        if (AddNormalinputs2(mtx, txfee, 4) < txfee) {  // TODO: in the previous code txfee was taken from 1of2 address
                            result.push_back(Pair("result", (char *)"error"));
                            result.push_back(Pair("error", (char *)"cant add normal inputs for txfee"));
                            return(result);
                        }
                        char lockInLoop1of2addr[KOMODO_ADDRESS_BUFSIZE], txidaddr[KOMODO_ADDRESS_BUFSIZE];
                        CPubKey createtxidPk = CCtxidaddr(txidaddr, loopData.createtxid);
                        GetCCaddress1of2(cp, lockInLoop1of2addr, Marmarapk, createtxidPk);  // 1of2 lock-in-loop address

                        CC *lockInLoop1of2cond = MakeCCcond1of2(EVAL_MARMARA, Marmarapk, createtxidPk);
                        CCAddVintxCond(cp, lockInLoop1of2cond, marmarapriv); //add probe condition to spend from the lock-in-loop address
                        cc_free(lockInLoop1of2cond);

                        LOGSTREAMFN("marmara", CCLOG_DEBUG1, stream << "calling AddMarmaraInputs for lock-in-loop addr=" << lockInLoop1of2addr << " adding amount=" << loopData.amount << std::endl);
                        if ((inputsum = AddMarmarainputs(IsLockInLoopOpret, mtx, pubkeys, lockInLoop1of2addr, loopData.amount, MARMARA_VINS)) >= loopData.amount)
                        {
                            change = (inputsum - loopData.amount);
                            mtx.vout.push_back(CTxOut(loopData.amount, CScript() << ParseHex(HexStr(currentpk)) << OP_CHECKSIG));   // locked-in-loop money is released to mypk doing the settlement
                            if (change > txfee) {
                                LOGSTREAMFN("marmara", CCLOG_ERROR, stream << "error: change not null=" << change << ", sent back to lock-in-loop addr=" << lockInLoop1of2addr << std::endl);
                                mtx.vout.push_back(MakeCC1of2vout(EVAL_MARMARA, change, Marmarapk, createtxidPk));
                            }
                            rawtx = FinalizeCCTx(0, cp, mtx, minerpk, txfee, MarmaraEncodeLoopSettlementOpret(true, loopData.createtxid, currentpk, 0), pubkeys);
                            if (rawtx.empty()) {
                                result.push_back(Pair("result", "error"));
                                result.push_back(Pair("error", "couldnt finalize CCtx"));
                                LOGSTREAMFN("marmara", CCLOG_ERROR, stream << " bad mtx=" << HexStr(E_MARSHAL(ss << mtx)) << std::endl);
                            }
                            else {
                                result.push_back(Pair("result", (char *)"success"));
                                result.push_back(Pair("hex", rawtx));
                                settlementTx = mtx;
                            }
                            return(result);
                        }

                        if (inputsum < loopData.amount)
                        {
                            int64_t remaining = loopData.amount - inputsum;
                            mtx.vout.push_back(CTxOut(txfee, CScript() << ParseHex(HexStr(CCtxidaddr(txidaddr, loopData.createtxid))) << OP_CHECKSIG)); // failure marker

                            // TODO: seems this was supposed that txfee should been taken from 1of2 address?
                            //if (refamount - remaining > 3 * txfee)
                            //    mtx.vout.push_back(CTxOut(refamount - remaining - 2 * txfee, CScript() << ParseHex(HexStr(mypk)) << OP_CHECKSIG));
                            mtx.vout.push_back(CTxOut(loopData.amount - remaining - txfee, CScript() << ParseHex(HexStr(currentpk)) << OP_CHECKSIG));

                            rawtx = FinalizeCCTx(0, cp, mtx, minerpk, txfee, MarmaraEncodeLoopSettlementOpret(false, loopData.createtxid, currentpk, -remaining), pubkeys);  //some remainder left
                            if (rawtx.empty()) {
                                result.push_back(Pair("result", "error"));
                                result.push_back(Pair("error", "couldnt finalize CCtx"));
                                LOGSTREAMFN("marmara", CCLOG_DEBUG1, stream << " bad mtx=" << HexStr(E_MARSHAL(ss << mtx)) << std::endl);
                            }
                            else {
                                result.push_back(Pair("result", (char *)"error"));
                                result.push_back(Pair("error", (char *)"insufficient funds"));
                                result.push_back(Pair("hex", rawtx));
                                result.push_back(Pair("remaining", ValueFromAmount(remaining)));
                            }
                        }
                        else
                        {
                            // jl777: maybe fund a txfee to report no funds avail
                            result.push_back(Pair("result", (char *)"error"));
                            result.push_back(Pair("error", (char *)"no funds available at all"));
                        }
                    }
                    /*else
                    {
                        result.push_back(Pair("result", (char *)"error"));
                        result.push_back(Pair("error", (char *)"this node does not have the baton"));
                        result.push_back(Pair("myCCaddr", myCCaddr));
                        result.push_back(Pair("batonCCaddr", batonCCaddr));
                    }*/
                }
                else
                {
                    result.push_back(Pair("result", (char *)"error"));
                    result.push_back(Pair("error", (char *)"couldnt get batontxid opret"));
                }
            }
            else
            {
                result.push_back(Pair("result", (char *)"error"));
                result.push_back(Pair("error", (char *)"couldnt find batontxid"));
            }
        }
        else
        {
            result.push_back(Pair("result", (char *)"error"));
            result.push_back(Pair("error", (char *)"couldnt get creation data"));
        }
    }
    else
    {
        result.push_back(Pair("result", (char *)"error"));
        result.push_back(Pair("error", (char *)"couldnt get creditloop for the baton"));
    }
    return(result);
}

// enum credit loops (for the pk or all if null pk passed)
// returns pending and closed txids
// calls callback with params batontxid and matures or -1 (if loop is closed)
template <class T>
static int32_t EnumCreditloops(int64_t &totalopen, std::vector<uint256> &issuances, int64_t &totalclosed, std::vector<uint256> &closed, struct CCcontract_info *cp, int32_t firstheight, int32_t lastheight, int64_t minamount, int64_t maxamount, CPubKey refpk, std::string refcurrency, T callback/*void (*callback)(uint256 batontxid, int32_t matures)*/)
{
    char marmaraaddr[KOMODO_ADDRESS_BUFSIZE]; 
    int32_t n = 0; 
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;
    CPubKey Marmarapk = GetUnspendable(cp, 0);
    GetCCaddress(cp, marmaraaddr, Marmarapk);
    SetCCunspents(unspentOutputs, marmaraaddr, true);

    // do all txid, conditional on spent/unspent
    LOGSTREAMFN("marmara", CCLOG_DEBUG2, stream  << "check on marmara addr=" << marmaraaddr << std::endl);
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it = unspentOutputs.begin(); it != unspentOutputs.end(); it++)
    {
        CTransaction issuancetx;
        uint256 hashBlock;
        uint256 issuancetxid = it->first.txhash;
        int32_t vout = (int32_t)it->first.index;

        LOGSTREAMFN("marmara", CCLOG_DEBUG2, stream << "checking tx as marker on marmara addr txid=" << issuancetxid.GetHex() << " vout=" << vout << std::endl);
        // enum creditloop markers:
        if (vout == MARMARA_MARKER_VOUT && GetTransaction(issuancetxid, issuancetx, hashBlock, true) && !hashBlock.IsNull())  // TODO: change to the locking or non-locking version if needed
        {
            if (!issuancetx.IsCoinBase() && issuancetx.vout.size() > 2 && issuancetx.vout.back().nValue == 0 /*has opreturn?*/)
            {
                struct CreditLoopOpret loopData;
                if (MarmaraDecodeLoopOpret(issuancetx.vout.back().scriptPubKey, loopData) == 'I')
                {
                    if (MarmaraGetLoopCreateData(loopData.createtxid, loopData) >= 0)
                    {
                        LOGSTREAMFN("marmara", CCLOG_DEBUG2, stream << "found issuance tx txid=" << issuancetxid.GetHex() << std::endl);
                        n++;
                        assert(!loopData.currency.empty());
                        assert(loopData.pk.size() != 0);
                        if (loopData.currency == refcurrency && loopData.matures >= firstheight && loopData.matures <= lastheight && loopData.amount >= minamount && loopData.amount <= maxamount && (refpk.size() == 0 || loopData.pk == refpk))
                        {
                            std::vector<uint256> creditloop;
                            uint256 batontxid;
                            LOGSTREAMFN("marmara", CCLOG_DEBUG2, stream << "issuance tx is filtered, txid=" << issuancetxid.GetHex() << std::endl);

                            if (MarmaraGetbatontxid(creditloop, batontxid, issuancetxid) > 0)
                            {
                                CTransaction batontx;
                                uint256 hashBlock;
                                uint8_t funcid;
                                struct CreditLoopOpret loopData;

                                LOGSTREAMFN("marmara", CCLOG_DEBUG2, stream << "found baton for txid=" << issuancetxid.GetHex() << std::endl);

                                if (GetTransaction(batontxid, batontx, hashBlock, true) && !hashBlock.IsNull() && batontx.vout.size() > 1 &&
                                    (funcid = MarmaraDecodeLoopOpret(batontx.vout.back().scriptPubKey, loopData)) != 0)
                                {
                                    assert(loopData.amount > 0);
                                    assert(loopData.matures > 0);
                                    if (funcid == 'D' || funcid == 'S') {
                                        // cannot get to here as the marker is spent in the settlement, so no closed loops to be listed!
                                        closed.push_back(issuancetxid);
                                        totalclosed += loopData.amount;
                                        callback(batontxid, -1);
                                    }
                                    else {
                                        issuances.push_back(issuancetxid);
                                        totalopen += loopData.amount;
                                        callback(batontxid, loopData.matures);
                                    }
                                }
                                else
                                    LOGSTREAMFN("marmara", CCLOG_ERROR, stream << "error getting of decoding batontx=" << batontxid.GetHex() << std::endl);
                            }
                            else
                                LOGSTREAMFN("marmara", CCLOG_ERROR, stream << "error finding baton for issuance txid=" << issuancetxid.GetHex() << std::endl);
                        }
                    }
                    else
                        LOGSTREAMFN("marmara", CCLOG_ERROR, stream << "error load create tx for createtxid=" << loopData.createtxid.GetHex() << std::endl);
                }
                else
                    LOGSTREAMFN("marmara", CCLOG_ERROR, stream << "incorrect funcid for issuancetxid=" << issuancetxid.GetHex() << std::endl);
            }
        }
        else
            LOGSTREAMFN("marmara", CCLOG_ERROR, stream  << "cant get tx on marmara marker addr (maybe still in mempool) txid=" << issuancetxid.GetHex() << std::endl);
    }
    return(n);
}

// adds to the passed vector the settlement transactions for all matured loops 
// called by the miner
// note that several or even all transactions might not fit into the current block, in this case they will be added on the next new block creation
// TODO: provide reserved space in the created block for at least some settlement transactions
void MarmaraRunAutoSettlement(int32_t height, std::vector<CTransaction> & settlementTransactions)
{
    int64_t totalopen, totalclosed;
    std::vector<uint256> issuances, closed;
    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_MARMARA);
    std::string funcname = __func__;

    int32_t firstheight = 0, lastheight = (1 << 30);
    int64_t minamount = 0, maxamount = (1LL << 60);

    LOGSTREAMFN("marmara", CCLOG_DEBUG1, stream << "starting enum open batons" << std::endl);

    EnumCreditloops(totalopen, issuances, totalclosed, closed, cp, firstheight, lastheight, minamount, maxamount, CPubKey(), MARMARA_CURRENCY, [&](uint256 batontxid, int32_t matures) 
    {
        CTransaction settlementtx;
        //TODO: temp UniValue result legacy code, change to remove UniValue

        if (chainActive.LastTip()->GetHeight() >= matures)   //check height if matured 
        {
            LOGSTREAM("marmara", CCLOG_DEBUG1, stream << funcname << " miner calling settlement for batontxid=" << batontxid.GetHex() << std::endl);

            UniValue result = MarmaraSettlement(0, batontxid, settlementtx);
            if (result["result"].getValStr() == "success") {
                LOGSTREAM("marmara", CCLOG_INFO, stream << funcname << " miner trying to add to block settlement tx=" << settlementtx.GetHash().GetHex() <<  ", for batontxid=" << batontxid.GetHex() << std::endl);
                settlementTransactions.push_back(settlementtx);
            }
            else {
                LOGSTREAM("marmara", CCLOG_ERROR, stream << funcname << " error=" << result["error"].getValStr() << " in settlement for batontxid=" << batontxid.GetHex() << std::endl);
            }
        }
    });
}

// create request tx for issuing or transfer baton (cheque) 
// the first call makes the credit loop creation tx
// txid of returned tx is approvaltxid
UniValue MarmaraReceive(int64_t txfee, CPubKey senderpk, int64_t amount, std::string currency, int32_t matures, int32_t avalcount, uint256 batontxid, bool automaticflag)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    UniValue result(UniValue::VOBJ); 
    struct CCcontract_info *cp, C; 
    int64_t requestFee; 
    std::string rawtx;

    cp = CCinit(&C, EVAL_MARMARA);
    if (txfee == 0)
        txfee = 10000;
    
    if (automaticflag != 0 && (matures & 1) == 0)
        matures++;
    else if (automaticflag == 0 && (matures & 1) != 0)
        matures++;

    CPubKey mypk = pubkey2pk(Mypubkey());
    uint256 createtxid = zeroid;
    const char *errorstr = NULL;

    if (batontxid != zeroid && MarmaraGetcreatetxid(createtxid, batontxid) < 0)
        errorstr = "cant get createtxid from batontxid";
    else if (currency != MARMARA_CURRENCY)
        errorstr = "for now, only MARMARA loops are supported";
    else if (amount <= txfee)
        errorstr = "amount must be for more than txfee";
    else if (matures <= chainActive.LastTip()->GetHeight())
        errorstr = "it must mature in the future";

    if (createtxid != zeroid) {
        // check original cheque params:
        CTransaction looptx;
        uint256 hashBlock;
        struct CreditLoopOpret loopData;

        if (!GetTransaction(batontxid.IsNull() ? createtxid : batontxid, looptx, hashBlock, true) ||
            hashBlock.IsNull() ||
            looptx.vout.size() < 1 ||
            MarmaraDecodeLoopOpret(looptx.vout.back().scriptPubKey, loopData) == 0)
        {
            LOGSTREAMFN("marmara", CCLOG_INFO, stream << "cant get looptx.GetHash()=" << looptx.GetHash().GetHex() << " looptx.vout.size()=" << looptx.vout.size() << std::endl);
            errorstr = "cant load previous loop tx or tx in mempool or cant decode tx opreturn data";
        }
        else if (senderpk != loopData.pk)
            errorstr = "current baton holder does not match the requested sender pk";

    }

    if (errorstr == NULL)
    {
        if (batontxid != zeroid)
            requestFee = txfee;
        else 
            requestFee = 2 * txfee;  // dimxy for the first time request tx the amount is 2 * txfee, why is this?
        if (AddNormalinputs(mtx, mypk, requestFee + txfee, 1) > 0)
        {
            mtx.vout.push_back(MakeCC1vout(EVAL_MARMARA, requestFee, senderpk));

            CScript opret;
            if (batontxid.IsNull())
                opret = MarmaraEncodeLoopCreateOpret(senderpk, amount, matures, currency);
            else
                opret = MarmaraEncodeLoopRequestOpret(createtxid, senderpk);

            rawtx = FinalizeCCTx(0, cp, mtx, mypk, txfee, opret);
            if (rawtx.size() == 0)
                errorstr = "couldnt finalize CCtx";
        }
        else 
            errorstr = "dont have enough normal inputs for requestfee and txfee";
    }
    if (rawtx.size() == 0 || errorstr != 0)
    {
        result.push_back(Pair("result", "error"));
        if (errorstr != 0)
            result.push_back(Pair("error", errorstr));
    }
    else
    {
        result.push_back(Pair("result", (char *)"success"));
        result.push_back(Pair("hex", rawtx));
        result.push_back(Pair("funcid", "R"));
        result.push_back(Pair("createtxid", createtxid.GetHex()));
        if (batontxid != zeroid)
            result.push_back(Pair("batontxid", batontxid.GetHex()));
        result.push_back(Pair("senderpk", HexStr(senderpk)));
        result.push_back(Pair("amount", ValueFromAmount(amount)));
        result.push_back(Pair("matures", matures));
        result.push_back(Pair("currency", currency));
    }
    return(result);
}


static int32_t RedistributeLockedRemainder(CMutableTransaction &mtx, struct CCcontract_info *cp, const std::vector<uint256> &creditloop, uint256 batontxid, CAmount amountToDistribute)
{
    CPubKey Marmarapk; 
    int32_t endorsersNumber = creditloop.size(); // number of endorsers, 0 is createtxid, last is 
    CAmount inputsum, change;
    std::vector <CPubKey> endorserPubkeys;
    CTransaction createtx;
    uint256 hashBlock, dummytxid;
    uint256 createtxid = creditloop[0];
    struct CreditLoopOpret loopData;

    uint8_t marmarapriv[32];
    Marmarapk = GetUnspendable(cp, marmarapriv);

    if (endorsersNumber < 1)  // nobody to return to
        return 0;

    if (GetTransaction(createtxid, createtx, hashBlock, false) && createtx.vout.size() > 1 &&
        MarmaraDecodeLoopOpret(createtx.vout.back().scriptPubKey, loopData) != 0)  // get amount value
    {
        char lockInLoop1of2addr[KOMODO_ADDRESS_BUFSIZE], txidaddr[KOMODO_ADDRESS_BUFSIZE];
        CPubKey createtxidPk = CCtxidaddr(txidaddr, createtxid);
        GetCCaddress1of2(cp, lockInLoop1of2addr, Marmarapk, createtxidPk);  // 1of2 lock-in-loop address 

        LOGSTREAMFN("marmara", CCLOG_DEBUG2, stream  << "calling AddMarmaraInputs for lock-in-loop addr=" << lockInLoop1of2addr << " adding as possible as amount=" << loopData.amount << std::endl);
        if ((inputsum = AddMarmarainputs(IsLockInLoopOpret, mtx, endorserPubkeys, lockInLoop1of2addr, loopData.amount, MARMARA_VINS)) >= loopData.amount / endorsersNumber) 
        {
            if (mtx.vin.size() >= CC_MAXVINS) {// vin number limit
                std::cerr  << " too many vins!" << std::endl;
                return -1;
            }

            if (endorserPubkeys.size() != endorsersNumber) {
                LOGSTREAMFN("marmara", CCLOG_ERROR, stream << " internal error not matched endorserPubkeys.size()=" << endorserPubkeys.size() << " endorsersNumber=" << endorsersNumber << " line=" << __LINE__ << std::endl);
                return -1;
            }

            CAmount amountReturned = 0;
            CAmount amountToPk = amountToDistribute / endorsersNumber;

            //for (int32_t i = 1; i < creditloop.size() + 1; i ++)  //iterate through all issuers/endorsers, skip i=0 which is 1st receiver tx, n + 1 is batontxid
            for (auto endorserPk : endorserPubkeys)
            {
                
                mtx.vout.push_back(CTxOut(amountToPk, CScript() << ParseHex(HexStr(endorserPk)) << OP_CHECKSIG));  // coins returned to each previous issuer normal 
                LOGSTREAMFN("marmara", CCLOG_DEBUG1, stream << " sending normal amount=" << amountToPk << " to pk=" << HexStr(endorserPk) << std::endl);
                amountReturned += amountToPk;
            }
            change = (inputsum - amountReturned);

            // return change to the lock-in-loop fund, distribute for pubkeys:
            if (change > 0) 
            {
                /* uncomment if the same check above is removed
                if (endorserPubkeys.size() != endorsersNumber) {
                    LOGSTREAMFN("marmara", CCLOG_ERROR, stream  << " internal error not matched endorsersPubkeys.size()=" << endorserPubkeys.size() << " endorsersNumber=" << endorsersNumber << " line=" << __LINE__ << std::endl);
                    return -1;
                } */
                for (auto pk : endorserPubkeys) {
                    CScript opret = MarmaraEncodeLoopCCVoutOpret(createtxid, pk);
                    vscript_t vopret;

                    GetOpReturnData(opret, vopret);
                    std::vector< vscript_t > vData{ vopret };    // add mypk to vout to identify who has locked coins in the credit loop
                    mtx.vout.push_back(MakeCC1of2vout(EVAL_MARMARA, change / endorserPubkeys.size(), Marmarapk, createtxidPk, &vData));  // TODO: losing remainder?

                    LOGSTREAMFN("marmara", CCLOG_DEBUG1, stream  << " distributing to loop change/pubkeys.size()=" << change / endorserPubkeys.size() << " vdata pk=" << HexStr(pk) << std::endl);
                }
            }

            CC *lockInLoop1of2cond = MakeCCcond1of2(EVAL_MARMARA, Marmarapk, createtxidPk);  
            CCAddVintxCond(cp, lockInLoop1of2cond, marmarapriv); //add probe condition to spend from the lock-in-loop address
            cc_free(lockInLoop1of2cond);

        }
        else  {
            LOGSTREAMFN("marmara", CCLOG_ERROR, stream  << " couldnt get lock-in-loop amount to return to endorsers" << std::endl);
            return -1;
        }
    }
    else {
        LOGSTREAMFN("marmara", CCLOG_ERROR, stream  << "could not load createtx" << std::endl);
        return -1;
    }
    return 0;
}


// issue or transfer coins to the next receiver
UniValue MarmaraIssue(int64_t txfee, uint8_t funcid, CPubKey receiverpk, const struct IssuerEndorserOptParams &optParams, uint256 requesttxid, uint256 batontxid)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    UniValue result(UniValue::VOBJ); 
    std::string rawtx; uint256 createtxid; 
    std::vector<uint256> creditloop;

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_MARMARA);

    if (txfee == 0)
        txfee = 10000;

    // make sure less than maxlength (?)

    CPubKey Marmarapk = GetUnspendable(cp, NULL);
    CPubKey mypk = pubkey2pk(Mypubkey());
    std::string errorstr;
    struct CreditLoopOpret loopData;

    if (MarmaraGetcreatetxid(createtxid, requesttxid) < 0)
        errorstr = "cant get createtxid from requesttxid";
    if (requesttxid.IsNull())
        errorstr = "requesttxid cant be empty";
    else if (mypk == receiverpk)
        errorstr = "cannot send baton to self";
    // TODO: extract and check the receiver pubkey

    if (errorstr.empty())
    {
        // check requested cheque params:
        CTransaction requestx;
        uint256 hashBlock;

        if( MarmaraGetLoopCreateData(createtxid, loopData) < 0 )
            errorstr = "cannot get loop creation data";
        else if (!GetTransaction(requesttxid, requestx, hashBlock, true) || 
            // TODO: do we need here to check the request tx in mempool?
            hashBlock.IsNull() /*is in mempool*/ || 
            requestx.vout.size() < 1 ||
            MarmaraDecodeLoopOpret(requestx.vout.back().scriptPubKey, loopData) == 0)
            errorstr = "cannot get request transaction or tx in mempool or cannot decode request tx opreturn data";
        else if (mypk != loopData.pk)
            errorstr = "mypk does not match the requested sender pk";
    }

    if (errorstr.empty())
    {
        char activated1of2addr[KOMODO_ADDRESS_BUFSIZE];
        int64_t inputsum;
        std::vector<CPubKey> pubkeys;

        uint256 dummytxid;
        int32_t endorsersNumber = MarmaraGetbatontxid(creditloop, dummytxid, requesttxid);  
        int64_t amountToLock = (endorsersNumber > 0 ? loopData.amount / (endorsersNumber + 1) : loopData.amount);  // include new endorser
        
        GetCCaddress1of2(cp, activated1of2addr, Marmarapk, mypk);  // 1of2 address where the activated endorser's money is locked

        LOGSTREAMFN("marmara", CCLOG_DEBUG2, stream  << "calling AddMarmaraInputs for activated addr=" << activated1of2addr << " needs activated amount to lock-in-loop=" << amountToLock << std::endl);
        if ((inputsum = AddMarmarainputs(IsActivatedOpret, mtx, pubkeys, activated1of2addr, amountToLock, MARMARA_VINS)) >= amountToLock) // add 1/n remainder from the locked fund
        {
            mtx.vin.push_back(CTxIn(requesttxid, 0, CScript()));  // spend the request tx (2*txfee for I, 1*txfee for T)
            if (funcid == 'T')
                mtx.vin.push_back(CTxIn(batontxid, 0, CScript()));   // spend the previous baton (1*txfee)

            if (funcid == 'T' || AddNormalinputs(mtx, mypk, txfee, 1) > 0)  // add one txfee more for marmaraissue
            {
                mtx.vout.push_back(MakeCC1vout(EVAL_MARMARA, txfee, receiverpk));  // vout0 is transfer of the baton to the next receiver
                if (funcid == 'I')
                    mtx.vout.push_back(MakeCC1vout(EVAL_MARMARA, txfee, Marmarapk));  // vout1 is marker of issuance tx

                // lock 1/N amount in loop
                char createtxidaddr[KOMODO_ADDRESS_BUFSIZE];
                CPubKey createtxidPk = CCtxidaddr(createtxidaddr, createtxid);

                // add cc lock-in-loop opret 
                CScript opret = MarmaraEncodeLoopCCVoutOpret(createtxid, mypk);
                vscript_t vopret;
                GetOpReturnData(opret, vopret);
                std::vector< vscript_t > vData{ vopret };  // add cc opret with mypk to cc vout 
                mtx.vout.push_back(MakeCC1of2vout(EVAL_MARMARA, amountToLock, Marmarapk, createtxidPk, &vData));

                LOGSTREAMFN("marmara", CCLOG_DEBUG1, stream  << " sending to loop amount=" << amountToLock << " marked with mypk=" << HexStr(mypk) << std::endl);

                // return change to my activated address:
                int64_t change = (inputsum - amountToLock);
                if (change > 0) 
                {
                    int32_t height = komodo_nextheight();
                    if ((height & 1) != 0) // make height even as only even height is considered for staking (TODO: strange)
                        height++;
                    CScript opret = MarmaraCoinbaseOpret('C', height, mypk);
                    vscript_t vopret;
                    GetOpReturnData(opret, vopret);
                    std::vector< vscript_t > vData{ vopret }; // add coinbase opret to ccvout for the change
                    mtx.vout.push_back(MakeCC1of2vout(EVAL_MARMARA, change, Marmarapk, mypk, &vData));  // adding MarmaraCoinbase cc vout 'opret' for change
                }

                if (endorsersNumber < 1 || RedistributeLockedRemainder(mtx, cp, creditloop, batontxid, amountToLock) >= 0)  // if there are issuers already then distribute and return amount / n value
                {
                    CC* activated1of2cond = MakeCCcond1of2(EVAL_MARMARA, Marmarapk, mypk);  // create vintx probe 1of2 cond
                    CCAddVintxCond(cp, activated1of2cond);      // add the probe to cp, it is copied and we can cc_free it
                    cc_free(activated1of2cond);

                    CScript opret;
                    if (funcid == 'I')
                        opret = MarmaraEncodeLoopIssuerOpret(createtxid, receiverpk, optParams.autoSettlement, optParams.autoInsurance, optParams.avalCount, optParams.disputeExpiresOffset, optParams.escrowOn, optParams.blockageAmount);
                    else
                        opret = MarmaraEncodeLoopTransferOpret(createtxid, receiverpk, optParams.avalCount);

                    rawtx = FinalizeCCTx(0, cp, mtx, mypk, txfee, opret);

                    if (rawtx.size() == 0) {
                        errorstr = "couldnt finalize CCtx";
                        LOGSTREAMFN("marmara", CCLOG_ERROR, stream  << " bad mtx=" << HexStr(E_MARSHAL(ss << mtx)) << std::endl);
                    }
                }
                else
                    errorstr = "could not return locked in loop funds to endorsers";
            }
            else
                errorstr = "dont have enough normal inputs for txfee";
        }
        else
            errorstr = "dont have enough locked inputs for amount";
    }
    if (!errorstr.empty())
    {
        result.push_back(Pair("result", "error"));
        result.push_back(Pair("error", errorstr));
    }
    else
    {
        result.push_back(Pair("result", (char *)"success"));
        result.push_back(Pair("hex", rawtx));
        char sfuncid[2]; 
        sfuncid[0] = funcid, sfuncid[1] = 0;
        result.push_back(Pair("funcid", sfuncid));
        result.push_back(Pair("createtxid", createtxid.GetHex()));
        result.push_back(Pair("approvaltxid", requesttxid.GetHex()));
        if (funcid == 'T')
            result.push_back(Pair("batontxid", batontxid.GetHex()));
        result.push_back(Pair("receiverpk", HexStr(receiverpk)));
//        result.push_back(Pair("amount", ValueFromAmount(amount)));
//        result.push_back(Pair("matures", matures));
//        result.push_back(Pair("currency", currency));
    }
    return(result);
}

UniValue MarmaraCreditloop(uint256 txid)
{
    UniValue result(UniValue::VOBJ), a(UniValue::VARR); 
    std::vector<uint256> creditloop; 
    uint256 batontxid, refcreatetxid, hashBlock; uint8_t funcid; 
    int32_t numerrs = 0, i, n; 
    CTransaction batontx; 
    char normaladdr[KOMODO_ADDRESS_BUFSIZE], myCCaddr[KOMODO_ADDRESS_BUFSIZE], destaddr[KOMODO_ADDRESS_BUFSIZE], batonCCaddr[KOMODO_ADDRESS_BUFSIZE]; 
    struct CCcontract_info *cp, C;
    struct CreditLoopOpret loopData;
    bool isSettledOk = false;

    cp = CCinit(&C, EVAL_MARMARA);
    if ((n = MarmaraGetbatontxid(creditloop, batontxid, txid)) > 0)
    {
        if (MarmaraGetLoopCreateData(creditloop[0], loopData) == 0)
        {
            if (GetTransaction(batontxid, batontx, hashBlock, false) && batontx.vout.size() > 1)
            {
                result.push_back(Pair("result", (char *)"success"));
                Getscriptaddress(normaladdr, CScript() << ParseHex(HexStr(Mypubkey())) << OP_CHECKSIG);
                result.push_back(Pair("myNormalAddress", normaladdr));
                GetCCaddress(cp, myCCaddr, Mypubkey());
                result.push_back(Pair("myCCaddress", myCCaddr));

                if ((funcid = MarmaraDecodeLoopOpret(batontx.vout.back().scriptPubKey, loopData)) != 0)
                {
                    std::string sfuncid(1, (char)funcid);
                    result.push_back(Pair("funcid", sfuncid));
                    result.push_back(Pair("currency", loopData.currency));

                    if (funcid == 'S')
                    {
                        refcreatetxid = creditloop[0];
                        result.push_back(Pair("settlement", batontxid.GetHex()));
                        result.push_back(Pair("createtxid", refcreatetxid.GetHex()));
                        result.push_back(Pair("remainder", ValueFromAmount(loopData.remaining)));
                        result.push_back(Pair("settled", loopData.matures));
                        result.push_back(Pair("pubkey", HexStr(loopData.pk)));
                        Getscriptaddress(normaladdr, CScript() << ParseHex(HexStr(loopData.pk)) << OP_CHECKSIG);
                        result.push_back(Pair("myNormalAddr", normaladdr));
                        result.push_back(Pair("collected", ValueFromAmount(batontx.vout[0].nValue)));
                        Getscriptaddress(destaddr, batontx.vout[0].scriptPubKey);
                        if (strcmp(normaladdr, destaddr) != 0)
                        {
                            result.push_back(Pair("destaddr", destaddr));
                            numerrs++;
                        }
                        isSettledOk = true;
                    }
                    else if (funcid == 'D')
                    {
                        refcreatetxid = creditloop[0];
                        result.push_back(Pair("settlement", batontxid.GetHex()));
                        result.push_back(Pair("createtxid", refcreatetxid.GetHex()));
                        result.push_back(Pair("remainder", ValueFromAmount(loopData.remaining)));
                        result.push_back(Pair("settled", loopData.matures));
                        Getscriptaddress(destaddr, batontx.vout[0].scriptPubKey);
                        result.push_back(Pair("txidaddr", destaddr));
                        if (batontx.vout.size() > 1)
                            result.push_back(Pair("collected", ValueFromAmount(batontx.vout[1].nValue)));
                    }
                    else
                    {
                        result.push_back(Pair("batontxid", batontxid.GetHex()));
                        result.push_back(Pair("createtxid", refcreatetxid.GetHex()));
                        result.push_back(Pair("amount", ValueFromAmount(loopData.amount)));
                        result.push_back(Pair("matures", loopData.matures));
                        if (refcreatetxid != creditloop[0])
                        {
                            LOGSTREAMFN("marmara", CCLOG_ERROR, stream << " invalid refcreatetxid, setting to creditloop[0]" << std::endl);
                            refcreatetxid = creditloop[0];
                            numerrs++;
                        }
                        result.push_back(Pair("batonpk", HexStr(loopData.pk)));
                        Getscriptaddress(normaladdr, CScript() << ParseHex(HexStr(loopData.pk)) << OP_CHECKSIG);
                        result.push_back(Pair("batonaddr", normaladdr));
                        GetCCaddress(cp, batonCCaddr, loopData.pk);  // baton address
                        result.push_back(Pair("batonCCaddr", batonCCaddr));
                        Getscriptaddress(normaladdr, batontx.vout[0].scriptPubKey);
                        if (strcmp(normaladdr, batonCCaddr) != 0)  // TODO: how is this possible?
                        {
                            result.push_back(Pair("vout0address", normaladdr));
                            numerrs++;
                        }

                        if (strcmp(myCCaddr, /*normaladdr*/batonCCaddr) == 0) // TODO: impossible with normal addr
                            result.push_back(Pair("ismine", 1));
                        else
                            result.push_back(Pair("ismine", 0));
                    }

                    // add locked-in-loop amount:
                    char lockInLoop1of2addr[KOMODO_ADDRESS_BUFSIZE], txidaddr[KOMODO_ADDRESS_BUFSIZE];
                    CPubKey createtxidPk = CCtxidaddr(txidaddr, refcreatetxid);
                    GetCCaddress1of2(cp, lockInLoop1of2addr, GetUnspendable(cp, NULL), createtxidPk);  // 1of2 lock-in-loop address 
                    std::vector<CPubKey> pubkeys;
                    CMutableTransaction mtx;
                    int64_t amountLockedInLoop = AddMarmarainputs(IsLockInLoopOpret, mtx, pubkeys, lockInLoop1of2addr, 0, 0);
                    result.push_back(Pair("LockedInLoopCCaddr", lockInLoop1of2addr));
                    result.push_back(Pair("LockedInLoopAmount", ValueFromAmount(amountLockedInLoop)));  // should be 0 if settled

                    for (i = 0; i < n; i++)
                    {
                        if (myGetTransaction(creditloop[i], batontx, hashBlock) != 0 && batontx.vout.size() > 1)
                        {
                            uint256 createtxid;
                            if ((funcid = MarmaraDecodeLoopOpret(batontx.vout.back().scriptPubKey, loopData)) != 0)
                            {
                                UniValue obj(UniValue::VOBJ);
                                obj.push_back(Pair("txid", creditloop[i].GetHex()));
                                std::string sfuncid(1, (char)funcid);
                                obj.push_back(Pair("funcid", sfuncid));
                                if (funcid == 'R' && createtxid == zeroid)
                                {
                                    createtxid = creditloop[i];
                                    obj.push_back(Pair("issuerpk", HexStr(loopData.pk)));
                                    Getscriptaddress(normaladdr, CScript() << ParseHex(HexStr(loopData.pk)) << OP_CHECKSIG);
                                    obj.push_back(Pair("issueraddr", normaladdr));
                                    GetCCaddress(cp, normaladdr, loopData.pk);
                                    obj.push_back(Pair("issuerCCaddr", normaladdr));
                                }
                                else
                                {
                                    obj.push_back(Pair("receiverpk", HexStr(loopData.pk)));
                                    Getscriptaddress(normaladdr, CScript() << ParseHex(HexStr(loopData.pk)) << OP_CHECKSIG);
                                    obj.push_back(Pair("receiveraddr", normaladdr));
                                    GetCCaddress(cp, normaladdr, loopData.pk);
                                    obj.push_back(Pair("receiverCCaddr", normaladdr));
                                }
                                Getscriptaddress(destaddr, batontx.vout[0].scriptPubKey);
                                if (strcmp(destaddr, normaladdr) != 0)
                                {
                                    obj.push_back(Pair("vout0address", destaddr));
                                    numerrs++;
                                }
                                if (i == 0 && isSettledOk)  // why isSettledOk checked?..
                                {
                                    result.push_back(Pair("amount", ValueFromAmount(loopData.amount)));
                                    result.push_back(Pair("matures", loopData.matures));
                                }
                                /* not relevant now as we do not copy params to new oprets
                                if (createtxid != refcreatetxid || amount != refamount || matures != refmatures || currency != refcurrency)
                                {
                                    numerrs++;
                                    obj.push_back(Pair("objerror", (char *)"mismatched createtxid or amount or matures or currency"));
                                    obj.push_back(Pair("createtxid", createtxid.GetHex()));
                                    obj.push_back(Pair("amount", ValueFromAmount(amount)));
                                    obj.push_back(Pair("matures", matures));
                                    obj.push_back(Pair("currency", currency));
                                } */
                                a.push_back(obj);
                            }
                        }
                    }
                    result.push_back(Pair("n", n));
                    result.push_back(Pair("numerrors", numerrs));
                    result.push_back(Pair("creditloop", a));
                }
                else
                {
                    result.push_back(Pair("result", (char *)"error"));
                    result.push_back(Pair("error", (char *)"couldnt get batontxid opret"));
                }
            }
            else
            {
                result.push_back(Pair("result", (char *)"error"));
                result.push_back(Pair("error", (char *)"couldnt find batontxid"));
            }
        }
        else
        {
            result.push_back(Pair("result", (char *)"error"));
            result.push_back(Pair("error", (char *)"couldnt get loop creation data"));
        }
    }
    else
    {
        result.push_back(Pair("result", (char *)"error"));
        result.push_back(Pair("error", (char *)"couldnt get creditloop"));
    }
    return(result);
}

// collect miner pool rewards (?)
UniValue MarmaraPoolPayout(int64_t txfee, int32_t firstheight, double perc, char *jsonstr) // [[pk0, shares0], [pk1, shares1], ...]
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    UniValue result(UniValue::VOBJ), a(UniValue::VARR); 
    cJSON *item, *array; std::string rawtx; 
    int32_t i, n; uint8_t buf[33]; 
    CPubKey Marmarapk, pk, poolpk; 
    int64_t payout, poolfee = 0, total, totalpayout = 0; 
    double poolshares, share, shares = 0.; 
    char *pkstr, *errorstr = 0; 
    struct CCcontract_info *cp, C;

    poolpk = pubkey2pk(Mypubkey());
    if (txfee == 0)
        txfee = 10000;
    cp = CCinit(&C, EVAL_MARMARA);
    Marmarapk = GetUnspendable(cp, 0);
    if ((array = cJSON_Parse(jsonstr)) != 0 && (n = cJSON_GetArraySize(array)) > 0)
    {
        for (i = 0; i < n; i++)
        {
            item = jitem(array, i);
            if ((pkstr = jstr(jitem(item, 0), 0)) != 0 && strlen(pkstr) == 66)
                shares += jdouble(jitem(item, 1), 0);
            else
            {
                errorstr = (char *)"all items must be of the form [<pubkey>, <shares>]";
                break;
            }
        }
        if (errorstr == 0 && shares > SMALLVAL)
        {
            shares += shares * perc;
            if ((total = AddMarmaraCoinbases(cp, mtx, firstheight, poolpk, 60)) > 0)
            {
                for (i = 0; i < n; i++)
                {
                    item = jitem(array, i);
                    if ((share = jdouble(jitem(item, 1), 0)) > SMALLVAL)
                    {
                        payout = (share * (total - txfee)) / shares;
                        if (payout > 0)
                        {
                            if ((pkstr = jstr(jitem(item, 0), 0)) != 0 && strlen(pkstr) == 66)
                            {
                                UniValue x(UniValue::VOBJ);
                                totalpayout += payout;
                                decode_hex(buf, 33, pkstr);
                                mtx.vout.push_back(MakeCC1of2vout(EVAL_MARMARA, payout, Marmarapk, buf2pk(buf)));
                                x.push_back(Pair(pkstr, (double)payout / COIN));
                                a.push_back(x);
                            }
                        }
                    }
                }
                if (totalpayout > 0 && total > totalpayout - txfee)
                {
                    poolfee = (total - totalpayout - txfee);
                    mtx.vout.push_back(MakeCC1of2vout(EVAL_MARMARA, poolfee, Marmarapk, poolpk));
                }
                rawtx = FinalizeCCTx(0, cp, mtx, poolpk, txfee, MarmaraCoinbaseOpret('P', firstheight, poolpk));
                if (rawtx.size() == 0)
                    errorstr = (char *)"couldnt finalize CCtx";
            }
            else errorstr = (char *)"couldnt find any coinbases to payout";
        }
        else if (errorstr == 0)
            errorstr = (char *)"no valid shares submitted";
        free(array);
    }
    else errorstr = (char *)"couldnt parse poolshares jsonstr";
    if (rawtx.size() == 0 || errorstr != 0)
    {
        result.push_back(Pair("result", "error"));
        if (errorstr != 0)
            result.push_back(Pair("error", errorstr));
    }
    else
    {
        result.push_back(Pair("result", (char *)"success"));
        result.push_back(Pair("hex", rawtx));
        if (totalpayout > 0 && total > totalpayout - txfee)
        {
            result.push_back(Pair("firstheight", firstheight));
            result.push_back(Pair("lastheight", ((firstheight / MARMARA_GROUPSIZE) + 1) * MARMARA_GROUPSIZE - 1));
            result.push_back(Pair("total", ValueFromAmount(total)));
            result.push_back(Pair("totalpayout", ValueFromAmount(totalpayout)));
            result.push_back(Pair("totalshares", shares));
            result.push_back(Pair("poolfee", ValueFromAmount(poolfee)));
            result.push_back(Pair("perc", ValueFromAmount((int64_t)(100. * (double)poolfee / totalpayout * COIN))));
            result.push_back(Pair("payouts", a));
        }
    }
    return(result);
}

// get all tx, constrain by vout, issuances[] and closed[]

UniValue MarmaraInfo(CPubKey refpk, int32_t firstheight, int32_t lastheight, int64_t minamount, int64_t maxamount, std::string currency)
{
    CMutableTransaction mtx; std::vector<CPubKey> pubkeys;
    UniValue result(UniValue::VOBJ), a(UniValue::VARR), b(UniValue::VARR); int32_t n; int64_t totalclosed = 0, totalamount = 0; std::vector<uint256> issuances, closed; 
    CPubKey Marmarapk; 
    char mynormaladdr[KOMODO_ADDRESS_BUFSIZE];
    char activated1of2addr[KOMODO_ADDRESS_BUFSIZE];
    char myccaddr[KOMODO_ADDRESS_BUFSIZE];

    struct CCcontract_info *cp, C;
    cp = CCinit(&C, EVAL_MARMARA);

    Marmarapk = GetUnspendable(cp, 0);
    result.push_back(Pair("result", "success"));
    
    Getscriptaddress(mynormaladdr, CScript() << ParseHex(HexStr(Mypubkey())) << OP_CHECKSIG);
    result.push_back(Pair("myNormalAddress", mynormaladdr));
    result.push_back(Pair("myNormalAmount", ValueFromAmount(CCaddress_balance(mynormaladdr, 0))));

    GetCCaddress1of2(cp, activated1of2addr, Marmarapk, Mypubkey());
    result.push_back(Pair("myCCActivatedAddress", activated1of2addr));
    result.push_back(Pair("myActivatedAmount", ValueFromAmount(AddMarmarainputs(IsActivatedOpret, mtx, pubkeys, activated1of2addr, 0, CC_MAXVINS)))); // changed MARMARA_VIN to CC_MAXVINS - we need actual amount
    result.push_back(Pair("myAmountOnActivatedAddress-old", ValueFromAmount(CCaddress_balance(activated1of2addr, 1))));

    GetCCaddress(cp, myccaddr, Mypubkey());
    result.push_back(Pair("myCCAddress", myccaddr));
    result.push_back(Pair("myCCBalance", ValueFromAmount(CCaddress_balance(myccaddr, 1))));

    // calc lock-in-loops amount for mypk:
    CAmount loopAmount = 0;
    CAmount totalLoopAmount = 0;
    char prevloopaddr[KOMODO_ADDRESS_BUFSIZE] = "";
    UniValue resultloops(UniValue::VARR);
    EnumMyLockedInLoop([&](char *loopaddr, const CTransaction & tx, int32_t nvout, CBlockIndex *pindex) // call enumerator with callback
    {
        //std::cerr << "lambda " << " loopaddr=" << loopaddr << " prevloopaddr=" << prevloopaddr << " loopAmount=" << loopAmount << std::endl;

        if (strcmp(prevloopaddr, loopaddr) != 0)   // loop address changed
        {
            if (prevloopaddr[0] != '\0')   // prevloop was
            {
                UniValue entry(UniValue::VOBJ);
                // if new loop then store amount for the prevloop
                entry.push_back(Pair("LoopAddress", prevloopaddr));
                entry.push_back(Pair("myAmountLockedInLoop", ValueFromAmount(loopAmount)));
                resultloops.push_back(entry);
                loopAmount = 0;  //reset for the next loop
            }
            strcpy(prevloopaddr, loopaddr);
        }
        loopAmount += tx.vout[nvout].nValue;
        totalLoopAmount += tx.vout[nvout].nValue;
    });
    if (prevloopaddr[0] != '\0') {   // last loop
        UniValue entry(UniValue::VOBJ);
        entry.push_back(Pair("LoopAddress", prevloopaddr));
        entry.push_back(Pair("myAmountLockedInLoop", ValueFromAmount(loopAmount)));
        resultloops.push_back(entry);
        //std::cerr << "lastloop " << " prevloopaddr=" << prevloopaddr << " loopAmount=" << loopAmount << std::endl;
    }
    result.push_back(Pair("Loops", resultloops));
    result.push_back(Pair("TotalLockedInLoop", ValueFromAmount(totalLoopAmount)));

    if (refpk.size() == 33)
        result.push_back(Pair("issuer", HexStr(refpk)));
    if (currency.size() == 0)
        currency = (char *)MARMARA_CURRENCY;
    if (firstheight <= lastheight)
        firstheight = 0, lastheight = (1 << 30);
    if (minamount <= maxamount)
        minamount = 0, maxamount = (1LL << 60);
    result.push_back(Pair("firstheight", firstheight));
    result.push_back(Pair("lastheight", lastheight));
    result.push_back(Pair("minamount", ValueFromAmount(minamount)));
    result.push_back(Pair("maxamount", ValueFromAmount(maxamount)));
    result.push_back(Pair("currency", currency));
    if ((n = EnumCreditloops(totalamount, issuances, totalclosed, closed, cp, firstheight, lastheight, minamount, maxamount, refpk, currency, [](uint256, int32_t) {/*do nothing*/})) > 0)
    {
        result.push_back(Pair("n", n));
        result.push_back(Pair("numpending", issuances.size()));
        for (int32_t i = 0; i < issuances.size(); i++)
            a.push_back(issuances[i].GetHex());
        result.push_back(Pair("issuances", a));
        result.push_back(Pair("totalamount", ValueFromAmount(totalamount)));
        result.push_back(Pair("numclosed", closed.size()));
        for (int32_t i = 0; i < closed.size(); i++)
            b.push_back(closed[i].GetHex());
        result.push_back(Pair("closed", b));
        result.push_back(Pair("totalclosed", ValueFromAmount(totalclosed)));
    }
    return(result);
}

uint32_t komodo_segid32(char *coinaddr);

// generate a new activated address and return its segid
UniValue MarmaraNewActivatedAddress(CPubKey pk)
{
    UniValue ret(UniValue::VOBJ);
    char activated1of2addr[KOMODO_ADDRESS_BUFSIZE];
    struct CCcontract_info *cp, C;

    cp = CCinit(&C, EVAL_MARMARA);
    CPubKey marmarapk = GetUnspendable(cp, 0);
    
    GetCCaddress1of2(cp, activated1of2addr, marmarapk, pk);
    CKeyID keyID = pk.GetID();
    std::string addr = EncodeDestination(keyID);

    ret.push_back(Pair("pubkey", HexStr(pk.begin(), pk.end())));
    ret.push_back(Pair("normaladdress", addr));
    ret.push_back(Pair("activated1of2address", activated1of2addr));
    ret.push_back(Pair("segid", (int32_t)komodo_segid32(activated1of2addr) & 0x3f));
    return ret;
}