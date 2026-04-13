module builder_comp;
import mesh_instruction_lib_cs;

ParameterBlock<mesh_instr::BuilderResources> res;

[shader("compute")]
[numthreads(128, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    mesh_instr::process_mesh_instruction(dispatchThreadID.x, res);
}