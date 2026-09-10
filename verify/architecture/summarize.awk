# One successful timing log only. Missing/new RESULT fields remain explicit.
BEGIN { FS="," }
/^step,/ { for(i=1;i<=NF;i++) col[$i]=i; next }
/^[0-9]+,/ { if("total_ms" in col){samples++; values[samples]=$(col["total_ms"])+0} next }
/^(RESULT|ACCOUNTING) / { line=$0;sub(/^[A-Z]+ /,"",line);raw=raw " " line; count=split(line,parts," "); for(i=1;i<=count;i++){split(parts[i],kv,"=");r[kv[1]]=kv[2]} }
function value(k) {return k in r?r[k]:"NA"}
function first(a,b,c) {if(a in r)return r[a];if(b in r)return r[b];if(c in r)return r[c];return "NA"}
END {
 for(i=2;i<=samples;i++){x=values[i];j=i-1;while(j>0&&values[j]>x){values[j+1]=values[j];j--}values[j+1]=x}
 med=samples?(samples%2?values[(samples+1)/2]:(values[samples/2]+values[samples/2+1])/2):"NA";
 p95=samples?values[int((samples*95+99)/100)]:"NA";
 printf "%s\t%s\t%s\t%s\t%s\t%d\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n",id,workload,batch,workers,variant,samples,med,p95,value("total_ms"),value("decisions_per_s"),first("actual_ticks_per_s","actual_subticks_per_s","ticks_per_s"),first("reset_count","reset_calls","resets"),first("reset_lanes","reset_lane_count","reset_envs"),raw;
}
